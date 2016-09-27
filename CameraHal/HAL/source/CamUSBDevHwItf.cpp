#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include "V4L2DevIoctr.h"
#include "CamUSBDevHwItfImc.h"
#include "common/return_codes.h"
#include "camHalTrace.h"

using namespace std;

CamUSBDevHwItf::Path::Path(V4L2DevIoctr *camDev, PATHID pathID, unsigned long dequeueTimeout):
	PathBase(NULL,camDev,pathID,dequeueTimeout),
	mUSBBufAllocator(new ProxyCameraBufferAllocator())
{
		ALOGD("%s: ", __func__);
}
	
CamUSBDevHwItf::Path::~Path()
{
	ALOGD("%s: ", __func__);
}


bool CamUSBDevHwItf::Path::prepare(
	frm_info_t  &frmFmt,
    unsigned int numBuffers,
    CameraBufferAllocator &bufferAllocator,
    bool cached,
    unsigned int minNumBuffersQueued)
{
	UNUSED_PARAM(bufferAllocator);
    shared_ptr<CameraBuffer> buffer;
    unsigned int stride = 0;
    unsigned int mem_usage = 0;

	
    //ALOGV("%s: %s format %s %dx%d@%d/%dfps, numBuffers %d, minNumBuffersQueued %d", __func__,
   //       string(mPathID), pixFmt, width, height, fps.mNumerator, fps.mDenominator, numBuffers, minNumBuffersQueued);

	if (( mState == STREAMING) || (mState == PREPARED)) {
		//prepare called when streaming, only the same format is allowd
		if (mCamDev->getCurFmt() != V4L2DevIoctr::halFmtToV4l2Fmt(frmFmt.frmFmt)) {
			ALOGW("format is different from current,req:%d,cur:%d",V4L2DevIoctr::halFmtToV4l2Fmt(frmFmt.frmFmt)
				,mCamDev->getCurFmt());
		}
		if	((mCamDev->getCurWidth() == frmFmt.frmSize.width)
			|| (mCamDev->getCurHeight() == frmFmt.frmSize.height))
			ALOGW("resolution is different from current,req:%dx%d,cur:%dx%d",
				frmFmt.frmSize.width,frmFmt.frmSize.height,
				mCamDev->getCurWidth(),mCamDev->getCurHeight());
		//just return ture
		return true;
	}
    if (mState != UNINITIALIZED) {
        ALOGE("%s: %d not in UNINITIALIZED state, cannot prepare path", __func__, mPathID);
        return false;
    }
    if (!numBuffers) {
        ALOGE("%s: %d number of buffers must be larger than 0", __func__, mPathID);
        return false;
    }

    releaseBuffers();

	frm_info_t infrmFmt = frmFmt;
	unsigned int inv4l2Fmt = V4L2DevIoctr::halFmtToV4l2Fmt(infrmFmt.frmFmt);
    if (0 > mCamDev->setFormat(inv4l2Fmt, infrmFmt.frmSize.width, infrmFmt.frmSize.height, 0)) {
        releaseBuffers();
        return false;
    } else {
		if (inv4l2Fmt != V4L2DevIoctr::halFmtToV4l2Fmt(frmFmt.frmFmt)) {
			ALOGE("%s:%d,required fmt dose not exist,request fmt(%s),best fmt(%s)",
				__func__,__LINE__,
				RK_HAL_FMT_STRING::hal_fmt_map_to_str(frmFmt.frmFmt),
				RK_HAL_FMT_STRING::hal_fmt_map_to_str(V4L2DevIoctr::V4l2FmtToHalFmt(inv4l2Fmt))
			);
			frmFmt.frmFmt = V4L2DevIoctr::V4l2FmtToHalFmt(inv4l2Fmt);
			frmFmt.frmSize = infrmFmt.frmSize;
        	releaseBuffers();
    		return false;
		} else if ((infrmFmt.frmSize.width != frmFmt.frmSize.width)
			|| (infrmFmt.frmSize.height != frmFmt.frmSize.height) )
		{
			ALOGW("%s:%d,required fmt dose not exist,request fmt(%s@%dx%d),best fmt(%s@%dx%d)",
				__func__,__LINE__,
				RK_HAL_FMT_STRING::hal_fmt_map_to_str(frmFmt.frmFmt),
				frmFmt.frmSize.width,frmFmt.frmSize.height,
				RK_HAL_FMT_STRING::hal_fmt_map_to_str(V4L2DevIoctr::V4l2FmtToHalFmt(inv4l2Fmt)),
				infrmFmt.frmSize.width,infrmFmt.frmSize.height
			);
			frmFmt.frmFmt = V4L2DevIoctr::V4l2FmtToHalFmt(inv4l2Fmt);
			frmFmt.frmSize = infrmFmt.frmSize;
		}
	}

    mNumBuffers = numBuffers;
    //mBufferAllocator = &bufferAllocator;
    mMinNumBuffersQueued = minNumBuffersQueued;
    if ((frmFmt.frmFmt == HAL_FRMAE_FMT_JPEG) || (mPathID == DMA))
        mNumBuffersUndequeueable = 0;
    else
        mNumBuffersUndequeueable = 1;

    mem_usage = (mPathID == DMA) ? CameraBuffer::READ : CameraBuffer::WRITE;
    if (cached) {
        mem_usage |= CameraBuffer::CACHED;
    }

	mem_usage |= CameraBuffer::FULL_RANGE;
    for (unsigned int i = 0; i < mNumBuffers; i++) {
        buffer = mUSBBufAllocator->alloc(RK_HAL_FMT_STRING::hal_fmt_map_to_str(frmFmt.frmFmt)
			, frmFmt.frmSize.width, frmFmt.frmSize.height, mem_usage,shared_from_this());
	
        if (buffer.get() == NULL) {
            releaseBuffers();
            return false;
        }
        buffer->setIndex(i);
        mBufferPool.push_back(buffer);
        if (!i)
            stride = buffer->getStride();
    }

    if (0 > mCamDev->requestBuffers(mNumBuffers)) {
        releaseBuffers();
        return false;
    }

    mNumBuffersQueued = 0;
    for (list<shared_ptr<CameraBuffer> >::iterator i = mBufferPool.begin(); i != mBufferPool.end(); i++) {
		//mmap buffer
		if (0 > mCamDev->memMap(*i)) {
			releaseBuffers();
			return false;
		}
        if (mCamDev->queueBuffer(*i) < 0) {
            releaseBuffers();
            return false;
        }
        mNumBuffersQueued++;
    }

    //ALOGV("%s: %s is now PREPARED", __func__, string(mPathID));

    mState = PREPARED;
    return true;
}


bool CamUSBDevHwItf::Path::prepare(
	frm_info_t &frmFmt,
	list<shared_ptr<CameraBuffer> > &bufPool,
    unsigned int numBuffers,
    unsigned int minNumBuffersQueued)
{
	UNUSED_PARAM(frmFmt);
	UNUSED_PARAM(bufPool);
	UNUSED_PARAM(numBuffers);
	UNUSED_PARAM(minNumBuffersQueued);
    return false;
}


void CamUSBDevHwItf::Path::addBufferNotifier(NewCameraBufferReadyNotifier *bufferReadyNotifier)
{
	osMutexLock(&mNotifierLock);
	if(bufferReadyNotifier)
		mBufferReadyNotifierList.push_back(bufferReadyNotifier);
	osMutexUnlock(&mNotifierLock);
	
}
bool CamUSBDevHwItf::Path::removeBufferNotifer(NewCameraBufferReadyNotifier *bufferReadyNotifier)
{
	bool ret = false;
	//search this notifier
	osMutexLock(&mNotifierLock);

	for (list<NewCameraBufferReadyNotifier *>::iterator i = mBufferReadyNotifierList.begin(); i != mBufferReadyNotifierList.end(); i++) {
        if (*i == bufferReadyNotifier) {
			mBufferReadyNotifierList.erase(i);
			ret = true;
			break;
        }
    }
	osMutexUnlock(&mNotifierLock);
	return ret;
}

bool CamUSBDevHwItf::Path::releaseBufToOwener(weak_ptr<CameraBuffer> camBuf)
{
    int ret = true;
	//decrese reference count  before queue
	osMutexLock(&mBufLock);
	shared_ptr<CameraBuffer> spBuf = camBuf.lock();
	if (spBuf.get() != NULL)
		{
		    ret = mCamDev->queueBuffer(spBuf);
		    if (!ret) {
				osMutexLock(&mNumBuffersQueuedLock);
		        mNumBuffersQueued++;
				osMutexUnlock(&mNumBuffersQueuedLock);
				osEventSignal(&mBufferQueued);
		    }
			else
				ret = false;
		}
	osMutexUnlock(&mBufLock);
    return ret;
}

void CamUSBDevHwItf::Path::releaseBuffers(void)
{
	//ALOGV("%s: line %d", __func__, __LINE__);

	if (mState == STREAMING) {
		ALOGD("%s: path is also be using.", __func__);	
		return;
	}
	osMutexLock(&mBufLock);
	//TODO:should wait all buffers are not used
	//ummap usb pool buffer
	for (list<shared_ptr<CameraBuffer> >::iterator i = mBufferPool.begin(); i != mBufferPool.end(); i++) {
		//mmap buffer
		if (((*i)->getVirtAddr() != NULL) && (0 > mCamDev->memUnmap(*i))) {
			ALOGE("%s: ummap usb pool buffer failed", __func__);
		}
	}
	 //release buffers after unmap,count 0 means free all allocated buffers
	mCamDev->requestBuffers(0);
	mBufferPool.clear();
	osMutexUnlock(&mBufLock);
}


bool CamUSBDevHwItf::Path::start(void)
{
    int ret;

    if (mState == STREAMING) {
        ALOGD("%s: %d is already in STREAMING state", __func__, mPathID);
		mPathRefCnt++;
        return true;
    } else if (mState != PREPARED) {
        ALOGE("%s: %d cannot start, path is not in PREPARED state", __func__, mPathID);
        return false;
    }
    if (mCamDev->streamOn())
        return false;
    mState = STREAMING;
	mPathRefCnt++;
	
    ret = mDequeueThread->run("pathTh");
    if (ret != RET_SUCCESS) {
        mState = PREPARED;
        ALOGE("%s: %d thread start failed (error %d)", __func__, mPathID, ret);
        return false;
    }
    //ALOGV("%s: %s is now STREAMING", __func__, string(mPathID));
    return true;
}

void CamUSBDevHwItf::Path::stop(void)
{
	ALOGD("%s: E", __func__);
    if (mState == STREAMING) {
		if (--mPathRefCnt != 0) {
			ALOGD("path also be used, not stop! pathRef %d", mPathRefCnt);
			return;
		}
    }
	
	osMutexLock(&mNumBuffersQueuedLock);
    if (mState == STREAMING) {
        mState = PREPARED;
		osEventSignal(&mBufferQueued);
		osMutexUnlock(&mNumBuffersQueuedLock);
        mDequeueThread->requestExitAndWait();
    } else
			osMutexUnlock(&mNumBuffersQueuedLock);
        

	osMutexLock(&mNumBuffersQueuedLock);
    if (mState == PREPARED) {
        mState = UNINITIALIZED;
		osMutexUnlock(&mNumBuffersQueuedLock);
        mCamDev->streamOff();
        mNumBuffersQueued = 0;
	} else
			osMutexUnlock(&mNumBuffersQueuedLock);
	//TODO should wait all buffers are returned.
    ALOGD("%s: %d is now UNINITIALIZED", __func__, mPathID);
}

CamUSBDevHwItf::CamUSBDevHwItf(void) {
	m_flag_init = false;

}
CamUSBDevHwItf::~CamUSBDevHwItf(void){

}

shared_ptr<CamHwItf::PathBase> CamUSBDevHwItf::getPath(enum PATHID id)
{
	shared_ptr<CamHwItf::PathBase> path;

	switch (id)
	{
		case MP:
		case SP:
			path = mSp;
			break;
		default:
			break;
	}
	return path;
}

// inputId used as video number
bool CamUSBDevHwItf::initHw(int inputId)
{
	bool ret = true;
	//open devices
			if (!m_flag_init) {
					char dev_name[15] = {0};
					snprintf(dev_name,15,"/dev/video%d",inputId);
#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
					m_cam_fd_overlay = open(dev_name, O_RDWR | O_NONBLOCK);
#else
					m_cam_fd_overlay = open(dev_name, O_RDWR);
#endif
					//LOGV("%s :m_cam_fd_overlay %d \n", __func__, m_cam_fd_overlay);
					if (m_cam_fd_overlay < 0) {
						LOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, dev_name, strerror(errno));
						return false;
					}

					mSpDev = shared_ptr<V4L2DevIoctr>(new V4L2DevIoctr(m_cam_fd_overlay,V4L2_BUF_TYPE_VIDEO_CAPTURE,V4L2_MEMORY_MMAP));
					mSp = shared_ptr<CamHwItf::PathBase>(new Path(mSpDev.get(),CamHwItf::SP));	
	
#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
					m_cam_fd_capture = m_cam_fd_overlay; 
#else
					m_cam_fd_capture = m_cam_fd_overlay;
#endif
					//LOGV("%s :m_cam_fd_capture %d \n", __func__, m_cam_fd_capture);
					if (m_cam_fd_capture < 0) {
						LOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, dev_name, strerror(errno));
						printf("%s:%d\n",__func__,__LINE__);
						return false;
					}
					
					mMpDev = mSpDev;
					mMp = mSp; 
	
#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
					m_cam_fd_dma = -1;
#else
					m_cam_fd_dma = -1;
#endif
					
					mDmaPathDev.reset(); 
					mDMAPath.reset();
					
					//LOGD("calling Query capabilityn\n");
					if (mSpDev->queryCap(V4L2_CAP_VIDEO_CAPTURE) < 0)
						ret = false;
					mInputId = inputId;			
					//LOGD("returned from Query capabilityn\n");
					m_flag_init = 1;
			}
	return ret;
}
void CamUSBDevHwItf::deInitHw()
{
	//LOGV("%s :", __func__);
	
	if (m_flag_init) {
	
		if (m_cam_fd_capture > -1) {
			mMp.reset();
			mMpDev.reset();
			//mp and sp have the same fd,
			//just close once
			//close(m_cam_fd_capture);
			m_cam_fd_capture = -1;
		}
	
		if (m_cam_fd_overlay > -1) {
			mSp.reset();
			mSpDev.reset();
			close(m_cam_fd_overlay);
			m_cam_fd_overlay = -1;
		}
	
		if (m_cam_fd_dma > -1) {
			mDmaPathDev.reset();
			mDMAPath.reset();
			close(m_cam_fd_dma);
			m_cam_fd_dma = -1;
		}
		m_flag_init = 0;
	}
}

