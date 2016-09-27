#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include "V4L2DevIoctr.h"
#include "CamIsp11CtrItf.h"
#include "CamIsp11DevHwItf.h"
#include "common/return_codes.h"
#include "camHalTrace.h"

using namespace std;

CamIsp11DevHwItf::Path::Path(CamIsp11DevHwItf *camIsp,
	V4L2DevIoctr *camDev,
	PATHID pathID,
	unsigned long dequeueTimeout):
	mCamIsp(camIsp),
	PathBase(camIsp,camDev,pathID,dequeueTimeout)
{

}
	
CamIsp11DevHwItf::Path::~Path()
{

}


bool CamIsp11DevHwItf::Path::prepare(
	frm_info_t  &frmFmt,
    unsigned int numBuffers,
    CameraBufferAllocator &bufferAllocator,
    bool cached,
    unsigned int minNumBuffersQueued)
{
    shared_ptr<CameraBuffer> buffer;
    unsigned int stride = 0;
    unsigned int mem_usage = 0;


    //ALOGV("%s: path id %d format %s %dx%d, numBuffers %d, minNumBuffersQueued %d", __func__,
    //      mPathID, RK_HAL_FMT_STRING::hal_fmt_map_to_str(frmFmt.frmFmt),
	//  frmFmt.frmSize.width,frmFmt.frmSize.height, numBuffers, minNumBuffersQueued);

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
    mBufferAllocator = &bufferAllocator;
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
        buffer = mBufferAllocator->alloc(RK_HAL_FMT_STRING::hal_fmt_map_to_str(frmFmt.frmFmt)
			, (frmFmt.frmSize.width), (frmFmt.frmSize.height), mem_usage,shared_from_this());
	
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
        if (mCamDev->queueBuffer(*i) < 0) {
            releaseBuffers();
            return false;
        }
				
		//mmap metada buffer
		mCamDev->memMap(*i);
        mNumBuffersQueued++;
    }

    //ALOGV("%s: %s is now PREPARED", __func__, string(mPathID));

    mState = PREPARED;
    return true;
}


bool CamIsp11DevHwItf::Path::prepare(
	frm_info_t &frmFmt,
	list<shared_ptr<CameraBuffer> > &bufPool,
    unsigned int numBuffers,
    unsigned int minNumBuffersQueued)
{
    shared_ptr<CameraBuffer> buffer;

    //ALOGV("%s: %s format %s %dx%d@%d/%dfps, stride %d, numBuffers %d, minNumBuffersQueued %d", __func__,
    //      string(mPathID), pixFmt, width, height, fps.mNumerator, fps.mDenominator, stride, numBuffers, minNumBuffersQueued);
	
	if (( mState == STREAMING) || (mState == PREPARED)) {
		//prepare called when streaming, only the same format is allowd
		if (mCamDev->getCurFmt() != V4L2DevIoctr::halFmtToV4l2Fmt(frmFmt.frmFmt)) {
			ALOGW("format is different from current,req:%d,cur:%d",
				V4L2DevIoctr::halFmtToV4l2Fmt(frmFmt.frmFmt),mCamDev->getCurFmt());
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
    mMinNumBuffersQueued = minNumBuffersQueued;
    mNumBuffersQueued = 0;
    if ((frmFmt.frmFmt == HAL_FRMAE_FMT_JPEG) || (mPathID == DMA))
        mNumBuffersUndequeueable = 0;
    else
        mNumBuffersUndequeueable = 1;

	unsigned int j = 0;
	for (list<shared_ptr<CameraBuffer> >::iterator i = bufPool.begin(); i != bufPool.end(); i++) {
		(*i)->setIndex(j++);
        mBufferPool.push_back(*i);
		if (j == numBuffers)
			break;

    }

    if (0 > mCamDev->requestBuffers(mNumBuffers)) {
        releaseBuffers();
        return false;
    }

	mNumBuffersQueued = 0;
    for (list<shared_ptr<CameraBuffer> >::iterator i = mBufferPool.begin(); i != mBufferPool.end(); i++) {
        if (mCamDev->queueBuffer(*i) < 0) {
            releaseBuffers();
            return false;
        }
				
		//mmap metada buffer
		mCamDev->memMap(*i);
        mNumBuffersQueued++;
    }
	
    //ALOGV("%s: %s is now PREPARED", __func__, string(mPathID));

    mState = PREPARED;
    return true;
}


void CamIsp11DevHwItf::Path::addBufferNotifier(NewCameraBufferReadyNotifier *bufferReadyNotifier)
{
	osMutexLock(&mNotifierLock);
	if(bufferReadyNotifier)
		mBufferReadyNotifierList.push_back(bufferReadyNotifier);
	osMutexUnlock(&mNotifierLock);
	
}
bool CamIsp11DevHwItf::Path::removeBufferNotifer(NewCameraBufferReadyNotifier *bufferReadyNotifier)
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

bool CamIsp11DevHwItf::Path::releaseBufToOwener(weak_ptr<CameraBuffer> camBuf)
{
    int ret = true;
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

void CamIsp11DevHwItf::Path::releaseBuffers(void)
{
	if (mState == STREAMING) {
		ALOGD("%s: path is also be using.", __func__);	
		return;
	}
	osMutexLock(&mBufLock);
	//TODO:should wait all buffers are not used
	for (list<shared_ptr<CameraBuffer> >::iterator i = mBufferPool.begin(); i != mBufferPool.end(); i++) {
		//mmap buffer
		if (0 > mCamDev->memUnmap(*i)) {
			ALOGE("%s: ummap metadata  buffer failed", __func__);
		}
	}
    	mBufferPool.clear();
	osMutexUnlock(&mBufLock);
}


bool CamIsp11DevHwItf::Path::start(void)
{
	int ret;
	struct isp_supplemental_sensor_mode_data sensor_mode_data;
	
	if (mState == STREAMING) {
		ALOGD("%s: %d is already in STREAMING state", __func__, mPathID);
		mPathRefCnt++;
		return true;
	} else if (mState != PREPARED) {
		ALOGE("%s: %d cannot start, path is not in PREPARED state", __func__, mPathID);
		return false;
	}

	ret = mCamDev->getSensorModeData(&sensor_mode_data);
	if (ret < 0) {
		ALOGE("%s: Path(%d) getSensorModeData failed", __func__,
			mPathID);
		return false;
	}

	if (mCamIsp->configIsp(&sensor_mode_data, true) < 0) {
		ALOGW("%s: Path(%d) configIsp failed", __func__,
			mPathID);
	}
	
	if (mCamDev->streamOn())
		return false;

	mState = STREAMING;
	mPathRefCnt++;
	mSkipFrames = 3;
	ret = mDequeueThread->run("pathTh");
	if (ret != RET_SUCCESS) {
		mState = PREPARED;
		ALOGE("%s: %d thread start failed (error %d)", __func__, mPathID, ret);
		return false;
	}
	return true;
}

void CamIsp11DevHwItf::Path::stop(void)
{
	//ALOGD("%s: E", __func__);
	if (mState == STREAMING) {
		if (--mPathRefCnt != 0) {
			ALOGD("path also be used, not stop! pathRef %d", mPathRefCnt);
			return;
		}
	}

	if (mCamIsp->configIsp(NULL, false) < 0) {
		ALOGW("%s: Path(%d) configIsp failed", __func__,
			mPathID);
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
	//ALOGD("%s: %d is now UNINITIALIZED", __func__, mPathID);
}

CamIsp11DevHwItf::CamIsp11DevHwItf(void) {
	
	//ALOGD("%s: E", __func__);
	m_flag_init = false;
	memset(&mIspCfg,0,sizeof(mIspCfg));
	osMutexInit(&mApiLock);
	//ALOGD("%s: x", __func__);
}
CamIsp11DevHwItf::~CamIsp11DevHwItf(void){
	//ALOGD("%s: E", __func__);
	//ALOGD("%s: x", __func__);

}
shared_ptr<CamHwItf::PathBase> CamIsp11DevHwItf::getPath(enum PATHID id)
{
	shared_ptr<CamHwItf::PathBase> path;

	switch (id)
	{
		case MP:
			path = mMp;
			break;
		case SP:
			path = mSp;
			break;
		case DMA:
			path = mDMAPath;
			break;
		default:
			break;
	}
	return path;
}
bool CamIsp11DevHwItf::initHw(int inputId)
{
	bool ret = true;
	struct camera_module_info_s camera_module;
	bool default_iq;
	//open devices
	if (!m_flag_init) {
#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
		m_cam_fd_overlay = open(CAMERA_OVERLAY_DEV_NAME, O_RDWR | O_NONBLOCK);
#else
		m_cam_fd_overlay = open(CAMERA_OVERLAY_DEV_NAME, O_RDWR);
#endif
		//LOGV("%s :m_cam_fd_overlay %d \n", __func__, m_cam_fd_overlay);
		if (m_cam_fd_overlay < 0) {
			LOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, CAMERA_OVERLAY_DEV_NAME, strerror(errno));
			return false;
		}

		mSpDev = shared_ptr<V4L2DevIoctr>(new V4L2ISPDevIoctr(m_cam_fd_overlay,V4L2_BUF_TYPE_VIDEO_OVERLAY,V4L2_MEMORY_USERPTR));
		mSp = shared_ptr<CamHwItf::PathBase>(new Path(this, mSpDev.get(),CamHwItf::SP));	

#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
		m_cam_fd_capture = open(CAMERA_CAPTURE_DEV_NAME, O_RDWR | O_NONBLOCK);
#else
		m_cam_fd_capture = open(CAMERA_CAPTURE_DEV_NAME, O_RDWR);
#endif
		//LOGV("%s :m_cam_fd_capture %d \n", __func__, m_cam_fd_capture);
		if (m_cam_fd_capture < 0) {
			LOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, CAMERA_CAPTURE_DEV_NAME, strerror(errno));
			printf("%s:%d\n",__func__,__LINE__);
			return false;
		}

		mMpDev = shared_ptr<V4L2DevIoctr>(new V4L2ISPDevIoctr(m_cam_fd_capture,V4L2_BUF_TYPE_VIDEO_CAPTURE,V4L2_MEMORY_USERPTR));
		mMp = shared_ptr<CamHwItf::PathBase>(new Path(this, mMpDev.get(),CamHwItf::MP));	

#ifdef CAMERAHAL_VIDEODEV_NONBLOCK
		m_cam_fd_dma = open(CAMERA_DMA_DEV_NAME, O_RDWR | O_NONBLOCK);
#else
		m_cam_fd_dma = open(CAMERA_DMA_DEV_NAME, O_RDWR);
#endif
		//LOGV("%s :m_cam_fd_dma %d \n", __func__, m_cam_fd_dma);
		if (m_cam_fd_dma < 0) {
			LOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, CAMERA_DMA_DEV_NAME, strerror(errno));
			printf("%s:%d\n",__func__,__LINE__);
			return false;
		}

		mDmaPathDev = shared_ptr<V4L2DevIoctr>(new V4L2DevIoctr(m_cam_fd_dma,V4L2_BUF_TYPE_VIDEO_OUTPUT,V4L2_MEMORY_USERPTR));
		mDMAPath = shared_ptr<CamHwItf::PathBase>(new Path(this, mDmaPathDev.get(),CamHwItf::DMA));	

		mISPDev = shared_ptr<CamIsp11CtrItf>(new CamIsp11CtrItf());
		if (mSpDev->queryCap(V4L2_CAP_VIDEO_OVERLAY) < 0)
			ret = false;
		if (mMpDev->queryCap(V4L2_CAP_VIDEO_CAPTURE) < 0)
			ret = false;
		//set input
		if (inputId < 0)
			ALOGE("%s:error input camera id %d",__func__,inputId); 

		mSpDev->setInput(inputId);

		default_iq = false;
		strcpy(camera_module.module_name,"(null)");
                strcpy(camera_module.len_name,"(null)");
                strcpy(camera_module.fov_h,"(null)");
                strcpy(camera_module.fov_v,"(null)");
                strcpy(camera_module.focus_distance,"(null)");
                strcpy(camera_module.focal_length,"(null)");
		camera_module.facing = -1;
                camera_module.orientation = -1;
                camera_module.iq_mirror = false;
                camera_module.iq_flip = false;
                camera_module.flash_support = -1;
		memset(mIqPath, 0x00, sizeof(mIqPath));
		
		if (mSpDev->getCameraModuleInfo(&camera_module) == 0) {
			if (!strcmp(camera_module.module_name,"(null)") && 
				!strcmp(camera_module.len_name,"(null)")) {
				ALOGW("Camera module name and len name haven't configured in kernel dts file,"
					"so used default tuning file!");
				default_iq = true;
			} else {
				if (strcmp(camera_module.module_name,"(null)")) {
					sprintf(mIqPath, "%s%s_%s.xml",
						CAMERA_IQ_DIR,
						camera_module.sensor_name,
						camera_module.module_name);
					if (0 != access(mIqPath, R_OK)) {
						if (strcmp(camera_module.len_name,"(null)")) {
							sprintf(mIqPath, "%s%s_%s.xml",
								CAMERA_IQ_DIR,
								camera_module.sensor_name,
								camera_module.len_name);
						}
					}
				} else {
					if (strcmp(camera_module.len_name,"(null)")) {
						sprintf(mIqPath, "%s%s_%s.xml",
							CAMERA_IQ_DIR,
							camera_module.sensor_name,
							camera_module.len_name);
					}
				}
				
				if (0 != access(mIqPath, R_OK)) {
					ALOGW("The iq file:%s isn't exist! so used default iq file",mIqPath);
					default_iq = true;
				}
			}

			if (default_iq == true) {
				sprintf(mIqPath, "%s", CAMERA_IQ_DEFAULT);
			}

		}
		
		mInputId = inputId;			
		m_flag_init = 1;
	}
	return ret;
}
void CamIsp11DevHwItf::deInitHw()
{
	//LOGV("%s :", __func__);
	
	//ALOGD("%s: E", __func__);
	if (m_flag_init) {
	
		if (m_cam_fd_capture > -1) {
			mMp.reset();
			mMpDev.reset();
			close(m_cam_fd_capture);
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
		mISPDev.reset();
		m_flag_init = 0;
		
		//ALOGD("%s: x", __func__);
	}
}

int CamIsp11DevHwItf::configIsp_l(struct isp_supplemental_sensor_mode_data *sensor)
{
	CamIspCtrItf::Configuration cfg;
	cfg = mIspCfg;
	/*config sensor mode data*/
	if (sensor && (
		(sensor->isp_input_width != mIspCfg.sensor_mode.isp_input_width) ||
		(sensor->isp_input_height != mIspCfg.sensor_mode.isp_input_height) ||
		(sensor->vt_pix_clk_freq_hz/1000000.0f != mIspCfg.sensor_mode.pixel_clock_freq_mhz) ||
		(sensor->crop_horizontal_start != mIspCfg.sensor_mode.horizontal_crop_offset) ||
		(sensor->crop_vertical_start != mIspCfg.sensor_mode.vertical_crop_offset) ||
		(sensor->crop_horizontal_end - sensor->crop_horizontal_start + 1 != mIspCfg.sensor_mode.cropped_image_width) ||
		(sensor->crop_vertical_end - sensor->crop_vertical_start + 1 != mIspCfg.sensor_mode.cropped_image_height) ||
		(sensor->line_length_pck != mIspCfg.sensor_mode.pixel_periods_per_line) ||
		(sensor->frame_length_lines != mIspCfg.sensor_mode.line_periods_per_field) ||
		(sensor->sensor_output_height != mIspCfg.sensor_mode.sensor_output_height) ||
		(sensor->fine_integration_time_min != mIspCfg.sensor_mode.fine_integration_time_min)  ||
		(sensor->line_length_pck - sensor->fine_integration_time_max_margin != mIspCfg.sensor_mode.fine_integration_time_max_margin) ||
		(sensor->coarse_integration_time_min != mIspCfg.sensor_mode.coarse_integration_time_min)  ||
                (sensor->coarse_integration_time_max_margin != mIspCfg.sensor_mode.coarse_integration_time_max_margin) ||
                (sensor->gain != mIspCfg.sensor_mode.gain) ||
                (sensor->exp_time != mIspCfg.sensor_mode.exp_time) ||
                (sensor->exposure_valid_frame != mIspCfg.sensor_mode.exposure_valid_frame))) {	

		cfg.sensor_mode.isp_input_width = sensor->isp_input_width;
		cfg.sensor_mode.isp_input_height = sensor->isp_input_height;
		cfg.sensor_mode.pixel_clock_freq_mhz = sensor->vt_pix_clk_freq_hz/1000000.0f;
		cfg.sensor_mode.horizontal_crop_offset = sensor->crop_horizontal_start;
		cfg.sensor_mode.vertical_crop_offset = sensor->crop_vertical_start;
		cfg.sensor_mode.cropped_image_width = sensor->crop_horizontal_end - sensor->crop_horizontal_start + 1;
		cfg.sensor_mode.cropped_image_height = sensor->crop_vertical_end - sensor->crop_vertical_start + 1;
		cfg.sensor_mode.pixel_periods_per_line =  sensor->line_length_pck;
		cfg.sensor_mode.line_periods_per_field = sensor->frame_length_lines;
		cfg.sensor_mode.sensor_output_height = sensor->sensor_output_height;
		cfg.sensor_mode.fine_integration_time_min = sensor->fine_integration_time_min;
		cfg.sensor_mode.fine_integration_time_max_margin = sensor->line_length_pck - sensor->fine_integration_time_max_margin;
		cfg.sensor_mode.coarse_integration_time_min = sensor->coarse_integration_time_min;
		cfg.sensor_mode.coarse_integration_time_max_margin = sensor->coarse_integration_time_max_margin;
      cfg.sensor_mode.gain = sensor->gain;
      cfg.sensor_mode.exp_time = sensor->exp_time;
      cfg.sensor_mode.exposure_valid_frame = sensor->exposure_valid_frame;
	}

	/*config controls*/
	if( (V4L2DevIoctr::V4l2FmtToHalFmt(mMpDev->getCurFmt())
		>= HAL_FRMAE_FMT_SBGGR10) && 
		(V4L2DevIoctr::V4l2FmtToHalFmt(mMpDev->getCurFmt())
		<= HAL_FRMAE_FMT_SRGGB8))
		cfg.uc = UC_RAW;
	else
		cfg.uc = UC_PREVIEW;
	cfg.aaa_locks = m3ALocks;
	cfg.aec_cfg.flk= mFlkMode;
	cfg.aec_cfg.mode = mCurAeMode;
	cfg.aec_cfg.meter_mode = mCurAeMeterMode;
	cfg.aec_cfg.ae_bias = mCurAeBias;
	//cfg.aec_cfg.win = ;
	cfg.afc_cfg.mode = mAfMode;
	//cfg.afc_cfg.win = ;
	cfg.awb_cfg.mode = m_wb_mode;
	//cfg.awb_cfg.win = ;
	cfg.cproc.brightness = mBrightness;
	cfg.cproc.contrast = mContrast;
	cfg.cproc.hue = mHue;
	cfg.cproc.saturation = mSaturation;
	//cfg.cproc.sharpness = ;
	cfg.flash_mode = mFlMode;
	cfg.ie_mode = m_image_effect;
	//TODO: ae bias,zoom,rotation,3a areas
	
	if (!mISPDev->configure(cfg)) {
		ALOGE("%s: mISPDev->configure failed!",
			__func__);
	}

	mIspCfg = cfg;
}

bool CamIsp11DevHwItf::configureISPModules(const void* config)
{
	return mISPDev->configureISP(config);
}

int CamIsp11DevHwItf::configIsp(
	struct isp_supplemental_sensor_mode_data *sensor,
	bool enable)
{

	osMutexLock(&mApiLock);
	if (enable) {
		if (sensor) {
			configIsp_l(sensor);
		}
		mISPDev->init(mIqPath, CAMERA_ISP_DEV_NAME, this);
		mISPDev->start();
	} else {
		mISPDev->stop();
		mISPDev->deInit();
	}
	osMutexUnlock(&mApiLock);

	return 0;
}

int CamIsp11DevHwItf::setExposure(unsigned int exposure, unsigned int gain, unsigned int gain_percent)
{
	int ret;
	struct v4l2_ext_control exp_gain[3];
	struct v4l2_ext_controls ctrls;
	
	exp_gain[0].id = V4L2_CID_EXPOSURE;
	
	exp_gain[0].value = exposure;
	exp_gain[1].id = V4L2_CID_GAIN;
	exp_gain[1].value = gain;
	exp_gain[2].id = RK_V4L2_CID_GAIN_PERCENT;
	exp_gain[2].value = gain_percent;
	
	ctrls.count = 3;
	ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
	ctrls.controls = exp_gain;
	ctrls.reserved[0] = 0;
	ctrls.reserved[1] = 0;
	
	ret = ioctl(m_cam_fd_overlay, VIDIOC_S_EXT_CTRLS, &ctrls);
	
	if (ret < 0) {
		LOGE("ERR(%s):set of  AE seting to sensor config failed! err: %s\n",
			__func__,
			strerror(errno));
		return ret;
	} else {
		mExposureSequence = exp_gain[0].value;
		TRACE_D(1,"%s(%d): mExposureSequence: %d",__FUNCTION__,__LINE__,mExposureSequence);
	}
	
	return ret;
}
int CamIsp11DevHwItf::setFPSCtrl()
{
	int ret = 0;
	
	if(m_last_fps_percent != m_fps_percent) {
		struct v4l2_ext_control exp_gain[2];
		struct v4l2_ext_controls ctrls;
	
		exp_gain[0].id = /*RK_V4L2_CID_FPS_CTRL*/0;
	
		exp_gain[0].value = m_fps_percent;
	
		ctrls.count = 1;
		ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
		ctrls.controls = exp_gain;
		ctrls.reserved[0] = 0;
		ctrls.reserved[1] = 0;
	
		//LOGD("%s new fps percent(%d)", __func__, m_fps_percent);
		ret = ioctl(m_cam_fd_overlay, VIDIOC_S_EXT_CTRLS, &ctrls);
	
		m_last_fps_percent = m_fps_percent;
		if (ret < 0) {
			LOGE("ERR(%s):set FPS Ctrl to sensor config failed\n", __func__);
			return ret;
		}
	}
	
	return ret;

}

void CamIsp11DevHwItf::transDrvMetaDataToHal
(
const void* drvMeta, 
struct HAL_Buffer_MetaData* halMeta
)
{
	/* IS raw format */
	if( (V4L2DevIoctr::V4l2FmtToHalFmt(mMpDev->getCurFmt())
		>= HAL_FRMAE_FMT_SBGGR10) && 
		(V4L2DevIoctr::V4l2FmtToHalFmt(mMpDev->getCurFmt())
		<= HAL_FRMAE_FMT_SRGGB8)) {
		struct v4l2_buffer_metadata_s* v4l2Meta = 
			(struct v4l2_buffer_metadata_s*)drvMeta;
		struct cifisp_isp_metadata* ispMetaData = 
			(struct cifisp_isp_metadata*)v4l2Meta->isp;
		if (ispMetaData) 
			mMpDev->getSensorModeData(&(ispMetaData->meas_stat.sensor_mode));
	}

	mISPDev->transDrvMetaDataToHal(drvMeta,halMeta);
}

/*
bool CamIsp11DevHwItf::enumSensorFmts(vector<frm_info_t> &frmInfos)
{
	UNUSED_PARAM(frmInfos);
	//TODO
	return false;
}
*/

int CamIsp11DevHwItf::setWhiteBalance(HAL_WB_MODE wbMode)
{
	//LOGV("%s set white balance mode:%d ", __func__, wbMode);
	
	if(m_wb_mode == wbMode)
		return 0;
	m_wb_mode = wbMode;
	configIsp_l(NULL);
	return 0;
}

int CamIsp11DevHwItf::setAntiBandMode(enum HAL_AE_FLK_MODE flkMode)
{
	if(mFlkMode == flkMode)
		return 0;
	mFlkMode = flkMode;
	configIsp_l(NULL);
	return 0;
}

int CamIsp11DevHwItf::setAeBias(int aeBias)
{
	if (mCurAeBias == aeBias) 
		return 0;
	mCurAeBias = aeBias;
	configIsp_l(NULL);
	return 0;
}



