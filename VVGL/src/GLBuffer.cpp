#include "GLBuffer.hpp"
#include "GLBufferPool.hpp"

#include <mutex>




namespace VVGL
{


using namespace std;




uint32_t GLBuffer::Descriptor::bytesPerRowForWidth(const uint32_t & w) const	{
	uint32_t		bytesPerRow = 4 * w;
	
	switch (this->pixelType)	{
	case PT_Float:
		switch (this->internalFormat)	{
#if !defined(VVGL_SDK_RPI)
		case IF_R:
			bytesPerRow = 32 * 1 * w / 8;
			break;
#endif
		default:
			bytesPerRow = 32 * 4 * w / 8;
			break;
		}
		break;
#if !defined(VVGL_SDK_RPI)
	case PT_HalfFloat:
		switch (this->internalFormat)	{
		case IF_R:
			bytesPerRow = 32 * 1 * w / 8;
			break;
		case IF_Depth24:
			bytesPerRow = 32 * 2 * w / 8;
			break;
		case IF_RGB:
			bytesPerRow = 32 * 3 * w / 8;
			break;
		case IF_RGBA:
		case IF_RGBA32F:
			bytesPerRow = 32 * 4 * w / 8;
			break;
		//case IF_RGBA16F:
		//	bytesPerRow = 16 * 4 * w / 8;
		//	break;
		default:
			break;
		}
		break;
#endif
	case PT_UByte:
		switch (this->internalFormat)	{
#if !defined(VVGL_SDK_RPI)
		case IF_R:
			bytesPerRow = 8 * 1 * w / 8;
			break;
#endif
		default:
			bytesPerRow = 8 * 4 * w / 8;
			break;
		}
		break;
#if !defined(VVGL_SDK_IOS) && !defined(VVGL_SDK_RPI)
	case PT_UInt_8888_Rev:
		bytesPerRow = 8 * 4 * w / 8;
		break;
#endif
	case PT_UShort88:
		bytesPerRow = 8 * 2 * w / 8;
		break;
	}
	
	switch (this->internalFormat)	{
#if !defined(VVGL_SDK_IOS) && !defined(VVGL_SDK_RPI)
	case IF_RGB_DXT1:
	case IF_A_RGTC:
		bytesPerRow = 4 * w / 8;
		break;
	case IF_RGBA_DXT5:
		bytesPerRow = 8 * w / 8;
		break;
#endif
	default:
		break;
	}
	return bytesPerRow;
}
uint32_t GLBuffer::Descriptor::backingLengthForSize(const Size & s) const	{
	return bytesPerRowForWidth(s.width) * s.height;
}


/*	========================================	*/
#pragma mark --------------------- create/destroy


GLBuffer::GLBuffer(GLBufferPoolRef inParentPool)	{
	//cout << __PRETTY_FUNCTION__ << endl;
	
	parentBufferPool = inParentPool;
}

GLBuffer::GLBuffer(const GLBuffer & n)	{
	//cout << __PRETTY_FUNCTION__ << endl;
	
	desc = n.desc;
	name = n.name;
	preferDeletion = n.preferDeletion;
	size = n.size;
	srcRect = n.srcRect;
	flipped = n.flipped;
	backingSize = n.backingSize;
	contentTimestamp = n.contentTimestamp;
	pboMapped = n.pboMapped;
	
	backingReleaseCallback = n.backingReleaseCallback;
	backingContext = n.backingContext;
	
	backingID = n.backingID;
	cpuBackingPtr = n.cpuBackingPtr;
#if defined(VVGL_SDK_MAC)
	//setUserInfo(n.getUserInfo());
	setLocalSurfaceRef(n.getLocalSurfaceRef());
	setRemoteSurfaceRef(n.getRemoteSurfaceRef());
#endif
	
	parentBufferPool = n.parentBufferPool;
}

GLBuffer::~GLBuffer()	{
	//cout << __PRETTY_FUNCTION__ << ", " << getDescriptionString() << endl;
	
	//	if this buffer was created by copying another buffer, clear out the source buffer
	if (copySourceBuffer != nullptr)	{
		//cout << "\tbuffer was copied, neither pooling nor releasing" << endl;
		copySourceBuffer = nullptr;
	}
	//	else this buffer was created (not copied) it has resources that need to be freed
	else	{
		//cout << "\tbuffer was created, may have resources to free\n";
		//	if the cpu backing was external, free the resource immediately (assume i can't write into it again)
		if (desc.cpuBackingType == Backing_External)	{
			//	if the gpu backing was internal, release it
			if (desc.gpuBackingType == Backing_Internal)	{
				if (parentBufferPool != nullptr)	{
					//(*parentBufferPool).releaseBufferResources(this);
					parentBufferPool->releaseBufferResources(this);
				}
			}
			//	else the gpu backing was external or non-existent: do nothing, the callback gets executed later no matter what
		}
		//	else the cpu backing as internal, or there's no cpu backing
		else	{
			//	if the gpu backing was internal, release it
			if (desc.gpuBackingType == Backing_Internal)	{
				//	if my idleCount is 0, i'm being freed from rendering and i go back in the pool
				if (idleCount==0 && !preferDeletion)	{
					if (parentBufferPool != nullptr)	{
						//(*parentBufferPool).returnBufferToPool(this);
						parentBufferPool->returnBufferToPool(this);
					}
				}
				//	else i was in the pool (or i just want to be deleted), and now the resources i contain need to be freed
				else	{
					if (parentBufferPool != nullptr)	{
						//(*parentBufferPool).releaseBufferResources(this);
						parentBufferPool->releaseBufferResources(this);
					}
				}
			}
			//	else this buffer was either external, or non-existent: do nothing
		}
		//	now call the backing release callback
		if (backingReleaseCallback != nullptr)	{
			backingReleaseCallback(*this, backingContext);
		}
	}
	
#if defined(VVGL_SDK_MAC)
	//setUserInfo(nullptr);
	setLocalSurfaceRef(nullptr);
	setRemoteSurfaceRef(nullptr);
#endif
	
	parentBufferPool = nullptr;
	
}
ostream & operator<<(ostream & os, const GLBuffer & n)	{
	//os << "<GLBuffer " << n.name << ", " << (int)n.size.width << "x" << (int)n.size.height << ">";
	
	os << n.getDescriptionString();
	return os;
}


GLBuffer * GLBuffer::allocShallowCopy()	{
	GLBuffer		*returnMe = new GLBuffer;
	returnMe->desc = desc;
	returnMe->name = name;
	returnMe->preferDeletion = preferDeletion;
	returnMe->size = size;
	returnMe->srcRect = srcRect;
	returnMe->flipped = flipped;
	returnMe->backingSize = backingSize;
	returnMe->contentTimestamp = contentTimestamp;
	returnMe->pboMapped = pboMapped;
	returnMe->backingReleaseCallback = backingReleaseCallback;
	returnMe->backingContext = backingContext;
	returnMe->backingID = backingID;
	returnMe->cpuBackingPtr = cpuBackingPtr;
#if defined(VVGL_SDK_MAC)
	returnMe->localSurfaceRef = (localSurfaceRef==NULL) ? NULL : (IOSurfaceRef)CFRetain(localSurfaceRef);
	returnMe->remoteSurfaceRef = (remoteSurfaceRef==NULL) ? NULL : (IOSurfaceRef)CFRetain(remoteSurfaceRef);
#endif	//	VVGL_SDK_MAC
	returnMe->parentBufferPool = parentBufferPool;
	returnMe->copySourceBuffer = copySourceBuffer;
	returnMe->idleCount = idleCount;
	return returnMe;
}


/*	========================================	*/
#pragma mark --------------------- getter/setter- mac stuff


#if defined(VVGL_SDK_MAC)
/*
id GLBuffer::getUserInfo() const	{
	return userInfo;
}
void GLBuffer::setUserInfo(id n)	{
	if (userInfo!=nil)
		CFRelease(userInfo);
	userInfo = (n==NULL) ? nullptr : (id)CFRetain(n);
}
*/
IOSurfaceRef GLBuffer::getLocalSurfaceRef() const	{
	return localSurfaceRef;
}

void GLBuffer::setLocalSurfaceRef(const IOSurfaceRef & n)	{
	if (localSurfaceRef != NULL)
		CFRelease(localSurfaceRef);
	localSurfaceRef = (IOSurfaceRef)n;
	if (localSurfaceRef != NULL)	{
		CFRetain(localSurfaceRef);
		desc.localSurfaceID = IOSurfaceGetID(localSurfaceRef);
		//	can't have a remote surface if i just made a local surface...
		setRemoteSurfaceRef(NULL);
	}
	else
		desc.localSurfaceID = 0;
}

IOSurfaceRef GLBuffer::getRemoteSurfaceRef() const	{
	return remoteSurfaceRef;
}

void GLBuffer::setRemoteSurfaceRef(const IOSurfaceRef & n)	{
	if (remoteSurfaceRef != NULL)
		CFRelease(remoteSurfaceRef);
	remoteSurfaceRef = (IOSurfaceRef)n;
	if (remoteSurfaceRef != NULL)	{
		CFRetain(remoteSurfaceRef);
		//	can't have a local surface if i've got a remote surface!
		setLocalSurfaceRef(NULL);
		
		preferDeletion = true;
	}
}
#endif	//	VVGL_SDK_MAC


/*	========================================	*/
#pragma mark --------------------- public methods


bool GLBuffer::isComparableForRecycling(const GLBuffer::Descriptor & n) const	{
	//	if any of these things DON'T match, return false- the comparison failed
	if ((desc.type != n.type)	||
	//(this->backingID != n.backingID)	||
	(desc.cpuBackingType != n.cpuBackingType)	||
	(desc.gpuBackingType != n.gpuBackingType)	||
	(desc.target != n.target)	||
	(desc.internalFormat != n.internalFormat)	||
	(desc.pixelFormat != n.pixelFormat)	||
	(desc.pixelType != n.pixelType) ||
	//(name != n.name) ||
	(desc.texRangeFlag != n.texRangeFlag)	||
	(desc.texClientStorageFlag != n.texClientStorageFlag)	||
	(desc.msAmount != n.msAmount)
	)	{
		return false;
	}
	
	//	...if i'm here, all of the above things matched
	
	//	if neither "wants" a local IOSurface, this is a match- return true
	if (desc.localSurfaceID==0 && n.localSurfaceID==0)
		return true;
	//	if both have a local IOSurface, this is a match- even if the local IOSurfaces aren't an exact match
	if (desc.localSurfaceID!=0 && n.localSurfaceID!=0)
		return true;
	
	return true;
}
uint32_t GLBuffer::backingLengthForSize(Size s) const	{
	return desc.backingLengthForSize(s);
}

Rect GLBuffer::glReadySrcRect() const	{
#if defined(VVGL_SDK_MAC)
	if (this->desc.target == Target_Rect)
		return srcRect;
#endif
	return { this->srcRect.origin.x/this->size.width, this->srcRect.origin.y/this->size.height, this->srcRect.size.width/this->size.width, this->srcRect.size.height/this->size.height };
}
/*
Rect GLBuffer::croppedSrcRect(Rect & cropRect, bool & takeFlipIntoAccount) const	{
	Rect		flippedCropRect = cropRect;
	if (takeFlipIntoAccount && this->flipped)
		flippedCropRect.origin.y = (1.0 - cropRect.size.height - cropRect.origin.y);
	
	Rect		returnMe = { 0., 0., 0., 0. };
	returnMe.size = { this->srcRect.size.width*flippedCropRect.size.width, this->srcRect.size.height*flippedCropRect.size.height };
	returnMe.origin.x = flippedCropRect.origin.x*this->srcRect.size.width + this->srcRect.origin.x;
	returnMe.origin.y = flippedCropRect.origin.y*this->srcRect.size.height + this->srcRect.origin.y;
	return returnMe;
}
*/
bool GLBuffer::isFullFrame() const	{
	if (this->srcRect.origin.x==0.0 && this->srcRect.origin.y==0.0 && this->srcRect.size.width==this->size.width && this->srcRect.size.height==this->size.height)
		return true;
	return false;
}

bool GLBuffer::isNPOT2DTex() const	{
	bool		returnMe = true;
	if (this->desc.target==Target_2D)	{
		int			tmpInt;
		tmpInt = 1;
		while (tmpInt<this->size.width)	{
			tmpInt <<= 1;
		}
		if (tmpInt==this->size.width)	{
			tmpInt = 1;
			while (tmpInt<this->size.height)	{
				tmpInt<<=1;
			}
			if (tmpInt==this->size.height)
				returnMe = false;
		}
	}
	else
		returnMe = false;
	return returnMe;
}

bool GLBuffer::isPOT2DTex() const	{
	bool		returnMe = false;
	if (this->desc.target==Target_2D)	{
		int			tmpInt;
		tmpInt = 1;
		while (tmpInt<this->size.width)	{
			tmpInt <<= 1;
		}
		if (tmpInt==this->size.width)	{
			tmpInt = 1;
			while (tmpInt<this->size.height)	{
				tmpInt<<=1;
			}
			if (tmpInt==this->size.height)
				returnMe = true;
		}
	}
	else
		returnMe = false;
	return returnMe;
}

#if defined(VVGL_SDK_MAC)
bool GLBuffer::safeToPublishToSyphon() const	{
	if (localSurfaceRef == nil)
		return false;
	if (flipped || desc.pixelFormat!=PF_BGRA)
		return false;
	if (srcRect.origin.x==0. && srcRect.origin.y==0. && srcRect.size.width==size.width && srcRect.size.height==size.height)
		return true;
	return false;
}
#endif
void GLBuffer::mapPBO(const uint32_t & inAccess, const bool & inUseCurrentContext)	{
	if (desc.type != Type_PBO || pboMapped)
		return;
	if (!inUseCurrentContext)	{
		GLContextRef		poolCtx = (parentBufferPool==nullptr) ? nullptr : parentBufferPool->getContext();
		if (poolCtx != nullptr)
			poolCtx->makeCurrentIfNotCurrent();
	}
	else	{
		//	intentionally blank- we're using the current thread's GL context...
	}
	
	glBindBufferARB(desc.target, name);
	cpuBackingPtr = glMapBufferARB(desc.target, inAccess);
	if (cpuBackingPtr != nullptr)
		pboMapped = true;
	glBindBufferARB(desc.target, 0);
}
void GLBuffer::unmapPBO(const bool & inUseCurrentContext)	{
	if (desc.type != Type_PBO || !pboMapped)
		return;
	if (!inUseCurrentContext)	{
		GLContextRef		poolCtx = (parentBufferPool==nullptr) ? nullptr : parentBufferPool->getContext();
		if (poolCtx != nullptr)
			poolCtx->makeCurrentIfNotCurrent();
	}
	else	{
		//	intentionally blank- we're using the current thread's GL context...
	}
	
	glBindBufferARB(desc.target, name);
	glUnmapBuffer(desc.target);
	glBindBufferARB(desc.target, 0);
	
	cpuBackingPtr = nullptr;
	pboMapped = false;
}
bool GLBuffer::isContentMatch(GLBuffer & n) const	{
	return this->contentTimestamp == n.contentTimestamp;
}
/*
void GLBuffer::draw(const Rect & dst) const	{
	if (desc.type != Type_Tex)
		return;
#if defined(VVGL_SDK_MAC)
	//inCtx.makeCurrentIfNotCurrent();
	float			verts[] = {
		(float)dst.minX(), (float)dst.minY(), 0.0,
		(float)dst.maxX(), (float)dst.minY(), 0.0,
		(float)dst.maxX(), (float)dst.maxY(), 0.0,
		(float)dst.minX(), (float)dst.maxY(), 0.0
	};
	Rect			src = glReadySrcRect();
	float			texs[] = {
		(float)src.minX(), (flipped) ? (float)src.maxY() : (float)src.minY(),
		(float)src.maxX(), (flipped) ? (float)src.maxY() : (float)src.minY(),
		(float)src.maxX(), (flipped) ? (float)src.minY() : (float)src.maxY(),
		(float)src.minX(), (flipped) ? (float)src.minY() : (float)src.maxY()
	};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	
	glVertexPointer(3, GL_FLOAT, 0, verts);
	glTexCoordPointer(2, GL_FLOAT, 0, texs);
	glBindTexture(desc.target, name);
	glDrawArrays(GL_QUADS, 0, 4);
	glBindTexture(desc.target, 0);
#else
	cout << "\tincomplete- " << __PRETTY_FUNCTION__ << endl;
#endif
	
}
*/
string GLBuffer::getDescriptionString() const	{
	//if (this == nullptr)
	//	return string("nullptr");
	char		typeChar = '?';
	switch (this->desc.type)	{
	case GLBuffer::Type_CPU:	typeChar='C'; break;
	case GLBuffer::Type_RB:	typeChar='R'; break;
	case GLBuffer::Type_FBO:	typeChar='F'; break;
	case GLBuffer::Type_Tex:	typeChar='T'; break;
	case GLBuffer::Type_PBO:	typeChar='P'; break;
	case GLBuffer::Type_VBO:	typeChar='V'; break;
	case GLBuffer::Type_EBO:	typeChar='E'; break;
	case GLBuffer::Type_VAO:	typeChar='A'; break;
	}
	return FmtString("<GLBuffer %c, %d, %dx%d>",typeChar,name,(int)this->size.width,(int)this->size.height);
}


/*	========================================	*/
#pragma mark --------------------- GLBuffer copy function


GLBufferRef GLBufferCopy(const GLBufferRef & n)	{
	//cout << __PRETTY_FUNCTION__ << endl;
	
	if (n==nullptr)
		return nullptr;
	GLBuffer			*srcBuffer = n.get();
	if (srcBuffer == nullptr)
		return nullptr;
	GLBufferRef		returnMe = make_shared<GLBuffer>(srcBuffer->parentBufferPool);
	GLBuffer			*newBuffer = returnMe.get();
	
	//(*newBuffer).desc = (*srcBuffer).desc;
	newBuffer->desc = srcBuffer->desc;
	
	newBuffer->name = srcBuffer->name;
	newBuffer->preferDeletion = true;	//	we want the copy to be deleted immediately
	newBuffer->size = srcBuffer->size;
	newBuffer->srcRect = srcBuffer->srcRect;
	newBuffer->flipped = srcBuffer->flipped;
	newBuffer->backingSize = srcBuffer->backingSize;
	newBuffer->contentTimestamp = srcBuffer->contentTimestamp;
	newBuffer->pboMapped = srcBuffer->pboMapped;
	newBuffer->backingID = srcBuffer->backingID;
	newBuffer->cpuBackingPtr = srcBuffer->cpuBackingPtr;
	
#if defined(VVGL_SDK_MAC)
	newBuffer->setLocalSurfaceRef(srcBuffer->getLocalSurfaceRef());
	newBuffer->setRemoteSurfaceRef(srcBuffer->getRemoteSurfaceRef());
#endif
	
	newBuffer->copySourceBuffer = n;	//	the copy needs a smart ptr so the buffer it's based on is retained
	
	return returnMe;
}




}

