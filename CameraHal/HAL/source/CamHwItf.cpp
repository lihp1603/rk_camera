
using namespace std;

#include "V4L2DevIoctr.h"
#include "CamHwItf.h"
#include "camHalTrace.h"
#ifdef RK_ISP10
#include "CamIsp10DevHwItf.h"
#endif
#ifdef RK_ISP11
#include "CamIsp11DevHwItf.h"
#endif
#include "CamHalVersion.h"
	
#ifdef RK_ISP10
		CamIsp10DevHwItf* mCamIsp10DevHwItf;
#endif
	
#ifdef RK_ISP11
		CamIsp11DevHwItf* mCamIsp11DevHwItf;
#endif

CamHwItf::PathBase::PathBase(CamHwItf *camHw,V4L2DevIoctr *camDev, PATHID pathID, unsigned long dequeueTimeout)
	:
	mCamHw(camHw),
	mCamDev(camDev),
	mPathID(pathID),
	mDequeueTimeout(dequeueTimeout),
	mState(UNINITIALIZED),
	mMinNumBuffersQueued(1),
	mNumBuffersUndequeueable(0),
	mBufferAllocator(NULL),
	mSkipFrames(0),
	mPathRefCnt(0),
	mDequeueThread(new DequeueThread(this)) 
{
	//ALOGD("%s: E", __func__);
	osMutexInit(&mNumBuffersQueuedLock);
	osMutexInit(&mPathLock);
	osMutexInit(&mBufLock);
	osMutexInit(&mNotifierLock);
	osEventInit(&mBufferQueued,true,0);
	//ALOGD("%s: X", __func__);
}
CamHwItf::PathBase::~PathBase(void) 
{
	//ALOGD("%s: E", __func__);
	osMutexDestroy(&mNumBuffersQueuedLock);
	osMutexDestroy(&mPathLock);
	osMutexDestroy(&mBufLock);
	osMutexDestroy(&mNotifierLock);
	osEventDestroy(&mBufferQueued);
	//ALOGD("%s: X", __func__);
}

bool CamHwItf::PathBase::dequeueFunc(void)
{
	int err;
	bool ret = true;
	shared_ptr<CameraBuffer> buffer;

	//ALOGV("%s: line %d", __func__, __LINE__);
	//mPathLock.lock();
	osMutexLock(&mNumBuffersQueuedLock);
	if (mState == STREAMING) {
	//mPathLock.unlock();

	if (mNumBuffersQueued > mNumBuffersUndequeueable) {
		osMutexUnlock(&mNumBuffersQueuedLock);
		err = mCamDev->dequeueBuffer(buffer, mDequeueTimeout);
	} else {
		osMutexUnlock(&mNumBuffersQueuedLock);
		osEventWait(&mBufferQueued);
	    return true;
	}

	if (err < 0) {
		//notify listenners
		osMutexLock(&mNotifierLock);
		weak_ptr<CameraBuffer> wpBuffer;
		for (list<NewCameraBufferReadyNotifier* >::iterator i = mBufferReadyNotifierList.begin(); i != mBufferReadyNotifierList.end(); i++) 
			ret = (*i)->bufferReady(wpBuffer, err);
		osMutexUnlock(&mNotifierLock);
	    if (err != -ETIMEDOUT) {
		ALOGE("%s: %d dequeue buffer failed, exiting thread loop", __func__, mPathID);
		ret = false;
	    } else {
		ALOGW("%s: %d dequeue timeout (%ldms)", __func__, mPathID, mDequeueTimeout);
	    }
	} else {
		if ((mSkipFrames > 0) || (mNumBuffersQueued <= mMinNumBuffersQueued)) {
			if (mSkipFrames > 0)
			    mSkipFrames--;
			mCamDev->queueBuffer(buffer);
			return true;
		}
		//get metadata
		struct HAL_Buffer_MetaData metaData;
		if (mCamDev->getBufferMetaData(buffer->getIndex(), &metaData)) {
			mCamHw->transDrvMetaDataToHal(metaData.metedata_drv, &metaData);
			buffer->setMetaData(&metaData);
			buffer->setTimestamp(&(metaData.timStamp));
		}
		osMutexLock(&mNumBuffersQueuedLock);
		mNumBuffersQueued--;
		osMutexUnlock(&mNumBuffersQueuedLock);
		//increase reference before notify
		osMutexLock(&mNotifierLock);
		buffer->incUsedCnt();
		for (list<NewCameraBufferReadyNotifier* >::iterator i = mBufferReadyNotifierList.begin(); i != mBufferReadyNotifierList.end(); i++) 
		{
			ret = (*i)->bufferReady(buffer, 0);
		}
		osMutexUnlock(&mNotifierLock);
		buffer->decUsedCnt();
	}
	
	} else {
		osMutexUnlock(&mNumBuffersQueuedLock);
		ALOGD("%s: %d stopped STREAMING", __func__, mPathID);
		ret = false;
	}

	if (!ret) {
		ALOGD("%s: %d exiting Thread loop", __func__, mPathID);
	}
	return ret;
}


bool CamHwItf::PathBase::setInput(Input inp) const
{
	if (mCamDev->setInput(inp))
		return false;
	return true;
}

unsigned int CamHwItf::PathBase::getMinNumUndequeueableBuffers(void) const
{
	return mMinNumBuffersQueued;
}
unsigned int CamHwItf::PathBase::getNumQueuedBuffers(void) const
{
	unsigned int numBuf;
	osMutexLock(&mNumBuffersQueuedLock);
	numBuf = mNumBuffersQueued;
	osMutexUnlock(&mNumBuffersQueuedLock);
	return numBuf;
}

CamHwItf::CamHwItf(void):m_flag_init(false)
{
	//ALOGD("%s: E", __func__);
	ALOGD("CAMHALVERSION is: %s\n",CAMHALVERSION);
	mCurAeMode = HAL_AE_OPERATION_MODE_AUTO;
	mCurAeBias = 0; 
	mCurAbsExp = -1;
	mCurAeMeterMode = HAL_AE_METERING_MODE_CENTER;
	mFlkMode = HAL_AE_FLK_AUTO;
	m_wb_mode = HAL_WB_AUTO;
	mAfMode = HAL_AF_MODE_AUTO;
	m3ALocks = HAL_3A_LOCKS_NONE;
	mSceneMode = HAL_SCENE_MODE_AUTO;
	mIsoMode = HAL_ISO_MODE_AUTO;
	m_image_effect = HAL_EFFECT_NONE;
	mZoom = 0;
	mBrightness = 0;
	mContrast = 0;
	mSaturation = 0;
	mHue = 0;
	mFlip = HAL_FLIP_NONE;
	//ALOGD("%s: X", __func__);
}

int CamHwItf::setAeMode(enum HAL_AE_OPERATION_MODE aeMode)
{
	int ret = 0;
	int mode = -1;
	switch (aeMode) {
		case HAL_AE_OPERATION_MODE_AUTO:
			 mode = V4L2_EXPOSURE_AUTO;
			break;
		case HAL_AE_OPERATION_MODE_MANUAL:
			mode = V4L2_EXPOSURE_MANUAL;
			break;
		case HAL_AE_OPERATION_MODE_LONG_EXPOSURE:
		case HAL_AE_OPERATION_MODE_ACTION:
		case HAL_AE_OPERATION_MODE_VIDEO_CONFERENCE:
		case HAL_AE_OPERATION_MODE_PRODUCT_TEST:
		case HAL_AE_OPERATION_MODE_ULL:
		case HAL_AE_OPERATION_MODE_FIREWORKS:
		default :
			ret = -1;
	}

	if (ret == 0) {
		//set ae
		struct v4l2_ext_control ctrls[1];
		ctrls[0].id = V4L2_CID_EXPOSURE_AUTO;
		ctrls[0].value = mode;
		if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
			ALOGE("%s:%d,set mode %d failed.",__func__,__LINE__,aeMode);
	}
	mCurAeMode = aeMode;

	return ret;
	
}
int CamHwItf::getSupportedAeModes(vector<enum HAL_AE_OPERATION_MODE> &aeModes)
{
	UNUSED_PARAM(aeModes);
	//TODO: may be got from XML config file
	return -1;
}
int CamHwItf::getAeMode(enum HAL_AE_OPERATION_MODE &aeMode)
{
	aeMode = mCurAeMode;
	return 0;
}
int CamHwItf::setAeBias(int aeBias)
{
	int ret = 0;
	if (mCurAeMode != HAL_AE_OPERATION_MODE_AUTO)
		return -1;
	//set ae bias
	struct v4l2_ext_control ctrls[1];
	ctrls[0].id = V4L2_CID_AUTO_EXPOSURE_BIAS;
	ctrls[0].value = aeBias;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
		ALOGE("%s:%d,set %d failed.",__func__,__LINE__,aeBias);

	mCurAeBias = aeBias;
	return ret;
}
int CamHwItf::getAeBias(int &curAeBias)
{
	curAeBias = mCurAeBias;
	return 0;
}

int CamHwItf::getSupportedBiasRange(HAL_RANGES_t &range)
{
	//UNUSED_PARAM(range);
	//TODO: may be got from XML files
	range.step = 50;
	range.min = -300;
	range.max = 300;
	return 0;
}

int CamHwItf::setAbsExp(int exposure)
{
#if 0
	int ret = 0;
	struct v4l2_ext_control ctrls[1];
	ctrls[0].id = V4L2_CID_EXPOSURE_ABSOLUTE;
	ctrls[0].value = exposure;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
		ALOGE("%s:%d,set %d failed.",__func__,__LINE__,exposure);

	mCurAbsExp = exposure;
	return ret;
#else
	if(mCurAbsExp == exposure)
		return 0;

	int ret = mSpDev->setCtrl(V4L2_CID_EXPOSURE, exposure);
	
	if(ret == 0)
		mCurAbsExp = exposure;
	else {
		LOGW("Could not set exposure, error %d", ret);
	}
	
	return ret;

#endif
}

int CamHwItf::getAbsExp(int &curExposure)
{
	curExposure = mCurAbsExp;
	return 0;
}

int CamHwItf::getSupportedExpMeterModes(vector<enum HAL_AE_METERING_MODE> modes)
{
	UNUSED_PARAM(modes);
	//TODO : may be got from xml file
	return -1;
}
int CamHwItf::setExposureMeterMode(enum HAL_AE_METERING_MODE aeMeterMode)
{
	int ret = 0;
	int mode = -1;
	switch (aeMeterMode) {
		case HAL_AE_METERING_MODE_AVERAGE:
			 mode = V4L2_EXPOSURE_METERING_AVERAGE;
			break;
		case HAL_AE_METERING_MODE_CENTER:
			mode = V4L2_EXPOSURE_METERING_CENTER_WEIGHTED ;
			break;
		case HAL_AE_METERING_MODE_SPOT:
			mode = V4L2_EXPOSURE_METERING_SPOT;
			break;
		case HAL_AE_METERING_MODE_MATRIX:
			mode = V4L2_EXPOSURE_METERING_MATRIX;
			break;
		default :
			ret = -1;
	}

	if (ret == 0) {
		//set ae
		struct v4l2_ext_control ctrls[1];
		ctrls[0].id = V4L2_CID_EXPOSURE_METERING;
		ctrls[0].value = mode;
		if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
			ALOGE("%s:%d,set mode %d failed.",__func__,__LINE__,aeMeterMode);
	}
	mCurAeMeterMode = aeMeterMode;

	return ret;
}
int CamHwItf::getExposureMeterMode(enum HAL_AE_METERING_MODE &aeMeterMode)
{
	aeMeterMode = mCurAeMeterMode;
	return 0;
}
int CamHwItf::setFps(HAL_FPS_INFO_t fps)
{
	//TODO: may be realized uncorrectly
	int ret = 0;
	struct v4l2_streamparm parm;
	//not set here,will be done in mSpDev->setStrmPara
	//parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = fps.numerator;
	parm.parm.capture.timeperframe.denominator = fps.denominator;
	if ( 0 > (ret = mSpDev->setStrmPara(&parm)))
		ALOGE("%s:%d,set fps %d failed.",__func__,__LINE__,fps);
	return ret;
}

int CamHwItf::enableAe(bool aeEnable)
{
	UNUSED_PARAM(aeEnable);
	//do nothing
	return 0;
}


int CamHwItf::getSupportedAntiBandModes(vector<enum HAL_AE_FLK_MODE> &flkModes)
{
	flkModes.push_back(HAL_AE_FLK_OFF);
	flkModes.push_back(HAL_AE_FLK_50);
	flkModes.push_back(HAL_AE_FLK_60);
	return 0;
}

int CamHwItf::setAntiBandMode(enum HAL_AE_FLK_MODE flkMode)
{
	int mode = -1,ret = 0;
	if(mFlkMode == flkMode)
		return 0;
	switch (flkMode) 
	{
		case HAL_AE_FLK_OFF:
			mode = V4L2_CID_POWER_LINE_FREQUENCY_DISABLED;
			break;
		case HAL_AE_FLK_50:
			mode = V4L2_CID_POWER_LINE_FREQUENCY_50HZ ;
			break;
		case HAL_AE_FLK_60:
			mode = V4L2_CID_POWER_LINE_FREQUENCY_60HZ  ;
			break;
		case HAL_AE_FLK_AUTO:
			mode = V4L2_CID_POWER_LINE_FREQUENCY_AUTO ;
			break;
		default :
			ret = -1;
	}

	if ( ret < 0)
		return -1;
	ret = mSpDev->setCtrl(V4L2_CID_POWER_LINE_FREQUENCY, mode);
	
	if(ret == 0)
		mFlkMode = flkMode;
	else {
		LOGW("Could not set antiband mode %d", flkMode);
	}
	
	return ret;
}
int CamHwItf::setExposure(unsigned int exposure, unsigned int gain, unsigned int gain_percent) {
#ifdef RK_ISP10
	mCamIsp10DevHwItf->setExposure(exposure, gain, gain_percent);
#endif

#ifdef RK_ISP11
	mCamIsp11DevHwItf->setExposure(exposure, gain, gain_percent);
#endif
	return 0;
}

//AWB
int CamHwItf::getSupportedWbModes(vector<HAL_WB_MODE> &modes)
{
	//TODO: may be got from xml file
	//UNUSED_PARAM(modes);
	modes.push_back(HAL_WB_AUTO);
	modes.push_back(HAL_WB_CLOUDY_DAYLIGHT);
	modes.push_back(HAL_WB_DAYLIGHT);
	modes.push_back(HAL_WB_FLUORESCENT);
	modes.push_back(HAL_WB_INCANDESCENT);
	return 0;
}
int CamHwItf::setWhiteBalance(HAL_WB_MODE wbMode)
{
	//LOGV("%s set white balance mode:%d ", __func__, wbMode);
	
	if(m_wb_mode == wbMode)
		return 0;
	
	enum v4l2_auto_n_preset_white_balance v4l2PresetWBMode;
	switch (wbMode) {
		case HAL_WB_CLOUDY_DAYLIGHT:
			v4l2PresetWBMode = V4L2_WHITE_BALANCE_CLOUDY;
			break;
		case HAL_WB_DAYLIGHT:
			v4l2PresetWBMode = V4L2_WHITE_BALANCE_DAYLIGHT;
			break;
		case HAL_WB_FLUORESCENT:
			v4l2PresetWBMode = V4L2_WHITE_BALANCE_FLUORESCENT;
			break;
		case HAL_WB_INCANDESCENT:
			v4l2PresetWBMode = V4L2_WHITE_BALANCE_INCANDESCENT;
			break;
		case HAL_WB_AUTO:
		default:
			v4l2PresetWBMode = V4L2_WHITE_BALANCE_AUTO;
			break;
	}

	int ret = mSpDev->setCtrl(V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, v4l2PresetWBMode);

	if (ret == 0)
		m_wb_mode = wbMode;
	else {
		LOGW("Could not set WB, error %d", ret);
	}
	
	return ret;
}
int CamHwItf::getWhiteBalanceMode(HAL_WB_MODE &wbMode)
{
	wbMode = m_wb_mode;
	return 0;
}
int CamHwItf::enableAwb(bool awbEnable)
{
	UNUSED_PARAM(awbEnable);
	//do nothing
	return 0;
}

//AF
int CamHwItf::setFocusPos(int position)
{
	//TODO : V4L2_CID_FOCUS_ABSOLUTE is a extended class control, could it be setted by mSpDev->setCtrl ?
	//TODO: should check current focus mode ?
	//LOGV("%s position %d 0x%x", __func__, position, V4L2_CID_FOCUS_ABSOLUTE);
	int ret = mSpDev->setCtrl(V4L2_CID_FOCUS_ABSOLUTE, position);
	
	if(ret < 0) {
		LOGE("Could not set focus, error %d", ret);
	}
	mLastLensPosition = position;
	return ret;
}
int CamHwItf::getFocusPos(int &position)
{
	position = mLastLensPosition;
	return 0;
}
int CamHwItf::getSupportedFocusModes(vector<enum HAL_AF_MODE> fcModes)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(fcModes);
	return -1;
}
int CamHwItf::setFocusMode(enum HAL_AF_MODE fcMode)
{
	int ret = 0;
	int mode = -1;
	switch (fcMode) {
		case HAL_AF_MODE_AUTO:
			 mode = V4L2_AUTO_FOCUS_RANGE_AUTO ;
			break;
		case HAL_AF_MODE_MACRO:
			mode = V4L2_AUTO_FOCUS_RANGE_MACRO  ;
			break;
		case HAL_AF_MODE_INFINITY:
			mode = V4L2_AUTO_FOCUS_RANGE_INFINITY;
			break;
		case HAL_AF_MODE_FIXED:
		case HAL_AF_MODE_EDOF:
		default :
			mode = V4L2_AUTO_FOCUS_RANGE_AUTO;
	}

	if ( (fcMode == HAL_AF_MODE_CONTINUOUS_VIDEO) ||(fcMode == HAL_AF_MODE_CONTINUOUS_PICTURE))
	{
		struct v4l2_ext_control ctrls[1];
		ctrls[0].id = V4L2_CID_FOCUS_AUTO;
		ctrls[0].value = 1;
		if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
			ALOGE("%s:%d,set mode %d failed.",__func__,__LINE__,fcMode);
	} else {
		struct v4l2_ext_control ctrls[1];
		if ((mAfMode == HAL_AF_MODE_CONTINUOUS_VIDEO) ||  (mAfMode == HAL_AF_MODE_CONTINUOUS_PICTURE))
		{
			//stop continuous focus firstly
			ctrls[0].id = V4L2_CID_FOCUS_AUTO;
			ctrls[0].value = 0;
			if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
				ALOGE("%s:%d,set mode %d failed.",__func__,__LINE__,fcMode);

		}
		ctrls[0].id = V4L2_CID_AUTO_FOCUS_RANGE;
		ctrls[0].value = mode;
		if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
			ALOGE("%s:%d,set mode %d failed.",__func__,__LINE__,fcMode);
	}
	mAfMode = fcMode;

	return ret;
}
int CamHwItf::getFocusMode(enum HAL_AF_MODE &fcMode)
{
	fcMode = mAfMode;
	return 0;
}
int CamHwItf::getAfStatus(enum HAL_AF_STATUS &afStatus)
{
	int ret = 0;
	struct v4l2_ext_control ctrls[1];
	ctrls[0].id = V4L2_CID_AUTO_FOCUS_STATUS;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
		ALOGE("%s:%d,start auto focus  failed.",__func__,__LINE__);
	else
		afStatus = (enum HAL_AF_STATUS)(ctrls[0].value);
	return ret;

}
int CamHwItf::trigggerAf(bool trigger)
{
	int ret = 0;
	if ((mAfMode == HAL_AF_MODE_AUTO) ||  (mAfMode == HAL_AF_MODE_MACRO)) {
		
		struct v4l2_ext_control ctrls[1];
		if (trigger)
			ctrls[0].id = V4L2_CID_AUTO_FOCUS_START;
		else
			ctrls[0].id = V4L2_CID_AUTO_FOCUS_STOP;
		ctrls[0].value = 1;
		if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
			ALOGE("%s:%d,trigger af %d  failed.",__func__,__LINE__,trigger);
	}
	return ret;
}
int CamHwItf::enableAf(bool afEnable)
{
	UNUSED_PARAM(afEnable);
	return -1;
}

int CamHwItf::getSupported3ALocks(vector<enum HAL_3A_LOCKS> &locks)
{
	UNUSED_PARAM(locks);
	//TODO:may be got from xml file
	return -1;
}
int CamHwItf::set3ALocks(int locks)
{
	int ret = 0;
	int setLock = locks & HAL_3A_LOCKS_ALL;
		
	struct v4l2_ext_control ctrls[1];
	ctrls[0].id = V4L2_CID_3A_LOCK;
	ctrls[0].value = setLock;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
		ALOGE("%s:%d,set 3A lock 0x%x  failed.",__func__,__LINE__,setLock);
	m3ALocks = setLock;
	return ret;
	
}
int CamHwItf::get3ALocks(int &curLocks)
{
	curLocks = m3ALocks;
	return 0;
}

bool CamHwItf::enumSupportedFmts(vector<RK_FRMAE_FORMAT> &frmFmts)
{
	vector<unsigned int> fmtVec;
	mSpDev->enumFormat(fmtVec);
	for( int i = 0; i < fmtVec.size(); i++) {
		frmFmts.push_back(mSpDev->V4l2FmtToHalFmt(fmtVec[i]));
	}
	return true;
}
bool CamHwItf::enumSupportedSizes(RK_FRMAE_FORMAT frmFmt,vector<frm_size_t> &frmSizes)
{
	mSpDev->enumFrmSize(mSpDev->halFmtToV4l2Fmt(frmFmt), frmSizes);
	return true;
}
bool CamHwItf::enumSupportedFps(RK_FRMAE_FORMAT frmFmt,frm_size_t frmSize,vector<HAL_FPS_INFO_t> &fpsVec)
{
	mSpDev->enumFrmFps(mSpDev->halFmtToV4l2Fmt(frmFmt),
		frmSize.width, frmSize.height,
		fpsVec);
	return true;
}
int  CamHwItf::tryFormat(frm_info_t inFmt, frm_info_t &outFmt)
{
	unsigned int v4l2PixFmt = mSpDev->halFmtToV4l2Fmt(inFmt.frmFmt);
	unsigned int width = inFmt.frmSize.width;
	unsigned int height = inFmt.frmSize.height;
	int ret = 0;
	if (0 > (ret = mSpDev->tryFormat(v4l2PixFmt,width,height)))
		ALOGE("%s:%d failed ,error is %s.",__func__,__LINE__,strerror(errno));
	else {
		outFmt.frmFmt = mSpDev->V4l2FmtToHalFmt(v4l2PixFmt);
		outFmt.frmSize.width = width;
		outFmt.frmSize.height = height;
		//TODO :try fps ?
		outFmt.fps = inFmt.fps;
	}
	return ret;
}

bool CamHwItf::enumSensorFmts(vector<frm_info_t> &frmInfos)
{
    //enum fmts
    vector<unsigned int> fmtVec;
	mSpDev->enumFormat(fmtVec);
	for( int i = 0; i < fmtVec.size(); i++) {
		//enum size for every fmt
		vector<frm_size_t> frmSizeVec;
		mSpDev->enumFrmSize(fmtVec[i], frmSizeVec);
		//enum fps for every size
		for( int j = 0; j < frmSizeVec.size(); j++) {
			vector<HAL_FPS_INFO_t> fpsVec;
			mSpDev->enumFrmFps(fmtVec[i],
				frmSizeVec[j].width, frmSizeVec[j].height,
				fpsVec);
			//copy out
			frm_info_t fmtInfo;
			fmtInfo.frmFmt = mSpDev->V4l2FmtToHalFmt(fmtVec[i]);
			fmtInfo.frmSize.width = frmSizeVec[j].width;
			fmtInfo.frmSize.height = frmSizeVec[j].height;
			for( int n = 0; n < fpsVec.size(); n++) { 
				fmtInfo.fps = fpsVec[n].denominator / fpsVec[n].numerator ;
				frmInfos.push_back(fmtInfo); 
			}
		}
	}
        return true;
}

//flash control
int CamHwItf::getSupportedFlashModes(vector<enum HAL_FLASH_MODE> &flModes)
{
	UNUSED_PARAM(flModes);
	//TODO:may be got from xml file
	return -1;
}
int CamHwItf::setFlashLightMode(enum HAL_FLASH_MODE flMode,int intensity,int timeout)
{
	int ret = 0;
	struct v4l2_ext_control ctrls[5];
	int ctrNum = 0;


	if (flMode == HAL_FLASH_OFF) {
		ctrls[0].id = V4L2_CID_FLASH_LED_MODE;
		ctrls[0].value = V4L2_FLASH_LED_MODE_NONE;
		ctrNum = 1;
	} else if (flMode == HAL_FLASH_ON) {
		ctrls[0].id = V4L2_CID_FLASH_LED_MODE;
		ctrls[0].value = V4L2_FLASH_LED_MODE_FLASH;
		
		ctrls[1].id = V4L2_CID_FLASH_INTENSITY;
		ctrls[1].value = intensity;
		
		ctrls[2].id = V4L2_CID_FLASH_TIMEOUT;
		ctrls[2].value = timeout;
		
		ctrls[3].id = V4L2_CID_FLASH_STROBE_SOURCE;
		ctrls[3].value = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;
		// should check V4L2_CID_FLASH_READY before strobe ?
		ctrls[4].id = V4L2_CID_FLASH_STROBE;
		ctrls[4].value = 1;
		ctrNum = 5;
	} else if (flMode == HAL_FLASH_TORCH) {
	
		ctrls[0].id = V4L2_CID_FLASH_LED_MODE;
		ctrls[0].value = V4L2_FLASH_LED_MODE_TORCH;
		
		ctrls[1].id = V4L2_CID_FLASH_TORCH_INTENSITY;
		ctrls[1].value = intensity;

		ctrNum = 2;
	}  
	
	if ( (ctrNum > 0 ) &&
			(0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CTRL_CLASS_FLASH ,ctrNum)))
		)
		ALOGE("%s:%d  failed.",__func__,__LINE__);
	mFlMode = flMode;
	return ret;
}
int CamHwItf::getFlashLightMode(enum HAL_FLASH_MODE &flMode)
{
	flMode = mFlMode;
	return true;
}
//color effect
int CamHwItf::getSupportedImgEffects(vector<enum HAL_IAMGE_EFFECT> &imgEffs)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(imgEffs);
	return -1;
}
int CamHwItf::setImageEffect(enum HAL_IAMGE_EFFECT image_effect)
{
	int ret = 0;
	//LOGV("%s %d 0x%x)", __func__, image_effect, V4L2_CID_COLORFX);
	
	enum v4l2_colorfx colorfx;
	enum HAL_IAMGE_EFFECT newEffect;
	
	switch(image_effect) {
		case HAL_EFFECT_SEPIA:
			colorfx = V4L2_COLORFX_SEPIA;
			newEffect = HAL_EFFECT_SEPIA;
			break;
		case HAL_EFFECT_MONO:
			colorfx = V4L2_COLORFX_BW;
			newEffect = HAL_EFFECT_MONO;
			break;
		case HAL_EFFECT_NEGATIVE:
			colorfx = V4L2_COLORFX_NEGATIVE;
			newEffect = HAL_EFFECT_NEGATIVE;
			break;
		case HAL_EFFECT_EMBOSS:
			colorfx = V4L2_COLORFX_EMBOSS;
			newEffect = HAL_EFFECT_EMBOSS;
			break;
		case HAL_EFFECT_SKETCH:
			colorfx = V4L2_COLORFX_SKETCH;
			newEffect = HAL_EFFECT_SKETCH;
			break;
		default:
			colorfx = V4L2_COLORFX_NONE;
			newEffect = HAL_EFFECT_NONE;
			break;
	}

	ret = mSpDev->setCtrl(V4L2_CID_COLORFX, (unsigned int) colorfx);
	if (ret < 0) {
		LOGE("ERR(%s):Fail on V4L2_CID_COLORFX", __func__);
		return ret;
	}
	
	m_image_effect = newEffect;
	return ret;
}
int CamHwItf::getImageEffect(enum HAL_IAMGE_EFFECT &image_effect)
{
	image_effect = m_image_effect;
	return true;
}

//scene
int CamHwItf::getSupportedSceneModes(vector<enum HAL_SCENE_MODE> &sceneModes)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(sceneModes);
	return -1;
}
int CamHwItf::setSceneMode(enum HAL_SCENE_MODE sceneMode)
{
	int ret = 0;
	int mode;
	switch (sceneMode) 
	{
		case HAL_SCENE_MODE_AUTO :
			mode = V4L2_SCENE_MODE_NONE;
			break;
		case HAL_SCENE_MODE_BACKLIGHT:
			mode = V4L2_SCENE_MODE_BACKLIGHT;
			break;
		case HAL_SCENE_MODE_BEACH_SNOW:
			mode = V4L2_SCENE_MODE_BEACH_SNOW;
			break;
		case HAL_SCENE_MODE_CANDLE_LIGHT:
			mode = V4L2_SCENE_MODE_CANDLE_LIGHT;
			break;
		case HAL_SCENE_MODE_DAWN_DUSK:
			mode = V4L2_SCENE_MODE_DAWN_DUSK;
			break;
		case HAL_SCENE_MODE_FALL_COLORS:
			mode = V4L2_SCENE_MODE_FALL_COLORS;
			break;
		case HAL_SCENE_MODE_FIREWORKS:
			mode = V4L2_SCENE_MODE_FIREWORKS;
			break;
		case HAL_SCENE_MODE_LANDSCAPE:
			mode = V4L2_SCENE_MODE_LANDSCAPE;
			break;
		case HAL_SCENE_MODE_NIGHT:
			mode = V4L2_SCENE_MODE_NIGHT;
			break;
		case HAL_SCENE_MODE_PARTY_INDOOR:
			mode = V4L2_SCENE_MODE_PARTY_INDOOR;
			break;
		case HAL_SCENE_MODE_PORTRAIT:
			mode = V4L2_SCENE_MODE_PORTRAIT;
			break;
		case HAL_SCENE_MODE_SPORTS:
			mode = V4L2_SCENE_MODE_SPORTS;
			break;
		case HAL_SCENE_MODE_SUNSET:
			mode = V4L2_SCENE_MODE_SUNSET;
			break;
		case HAL_SCENE_MODE_TEXT:
			mode = V4L2_SCENE_MODE_TEXT;
			break;
		default:
			ALOGE("%s:%d,not support this mode %d.",__func__,__LINE__,sceneMode);
			return -1;
	}
	
	struct v4l2_ext_control ctrls[1];
	ctrls[0].id = V4L2_CID_SCENE_MODE ;
	ctrls[0].value = mode;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,1)))
		ALOGE("%s:%d,set mode %d  failed.",__func__,__LINE__,sceneMode);
	mSceneMode = sceneMode;
	return ret;
}
int CamHwItf::getSceneMode(enum HAL_SCENE_MODE &sceneMode)
{
	sceneMode = mSceneMode;
	return 0;
}

int CamHwItf::getSupportedISOModes(vector<enum HAL_ISO_MODE> &isoModes)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(isoModes);
	return -1;
}
int CamHwItf::setISOMode(enum HAL_ISO_MODE isoMode, int sens )
{
	int ret = 0;
	int mode;
	struct v4l2_ext_control ctrls[2];
	int ctrNum = 1;
	if (isoMode == HAL_ISO_MODE_AUTO)
	{
		ctrls[0].id = V4L2_CID_ISO_SENSITIVITY_AUTO  ;
		ctrls[0].value = mode;
	} else {
		ctrls[0].id = V4L2_ISO_SENSITIVITY_MANUAL  ;
		ctrls[0].value = mode;
		ctrls[1].id = V4L2_CID_ISO_SENSITIVITY  ;
		ctrls[1].value = sens;
		ctrNum = 2;
	}
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,ctrNum)))
		ALOGE("%s:%d,set mode %d  failed.",__func__,__LINE__,isoMode);
	mIsoMode = isoMode;
	return ret;
}
int CamHwItf::getISOMode(enum HAL_ISO_MODE &isoMode )
{
	isoMode = mIsoMode;
	return 0;
}

int CamHwItf::getSupportedZoomRange(HAL_RANGES_t &zoomRange)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(zoomRange);
	return -1;
}
int CamHwItf::setZoom(int zoomVal)
{
	int ret = 0;
	struct v4l2_ext_control ctrls[2];
	int ctrNum = 1;
	ctrls[0].id = V4L2_CID_ZOOM_ABSOLUTE ;
	ctrls[0].value = zoomVal;
	if ( 0 > (ret = mSpDev->setExtCtrls(ctrls,V4L2_CID_CAMERA_CLASS ,ctrNum)))
		ALOGE("%s:%d,set  %d  failed.",__func__,__LINE__,zoomVal);
	mZoom = zoomVal;
	return ret;
}
int CamHwItf::getZoom(int &zoomVal)
{
	zoomVal = mZoom;
	return 0;
}

//brightness
int CamHwItf::getSupportedBtRange(HAL_RANGES_t &brightRange)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(brightRange);
	return -1;
}
int CamHwItf::setBrightness(int brightVal)
{
	int ret = mSpDev->setCtrl(V4L2_CID_BRIGHTNESS, brightVal);
	
	if(ret == 0)
		mBrightness = brightVal;
	else {
		LOGW("Could not set Brightness, error %d", ret);
	}
	
	return ret;
}
int CamHwItf::getBrithtness(int &brightVal)
{
	mBrightness = brightVal;
	return 0;
}
//contrast
int CamHwItf::getSupportedCtRange(HAL_RANGES_t &contrastRange)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(contrastRange);
	return -1;
}
int CamHwItf::setContrast(int contrast)
{
	int ret = mSpDev->setCtrl(V4L2_CID_CONTRAST, contrast);
	
	if(ret == 0)
		mContrast = contrast;
	else {
		LOGW("Could not set contrast, error %d", ret);
	}
	
	return ret;
}
int CamHwItf::getContrast(int &contrast)
{
	mContrast = contrast;
	return 0;
}
//saturation
int CamHwItf::getSupportedStRange(HAL_RANGES_t &saturationRange)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(saturationRange);
	return -1;
}
int CamHwItf::setSaturation(int sat)
{
	int ret = mSpDev->setCtrl(V4L2_CID_SATURATION, sat);
	
	if(ret == 0)
		mSaturation = sat;
	else {
		LOGW("Could not set saturation, error %d", ret);
	}
	
	return ret;
}
int CamHwItf::getSaturation(int &sat)
{
	mSaturation = sat;
	return 0;
}
//hue
int CamHwItf::getSupportedHueRange(HAL_RANGES_t &hueRange)
{
	//TODO:may be got from xml file
	UNUSED_PARAM(hueRange);
	return -1;
}
int CamHwItf::setHue(int hue)
{
	int ret = mSpDev->setCtrl(V4L2_CID_HUE, hue);
	
	if(ret == 0)
		mHue = hue;
	else {
		LOGW("Could not set hue, error %d", ret);
	}
	
	return ret;
}
int CamHwItf::getHue(int &hue)
{
	mHue = hue;
	return 0;
}

int CamHwItf::setFlip(int flip)
{
	int ret = 0;
	int setFlip = flip & (HAL_FLIP_H|HAL_FLIP_V);
	int needSetFlip = setFlip & ~mFlip;
	if (needSetFlip & HAL_FLIP_H)
		ret = mSpDev->setCtrl(V4L2_CID_HFLIP, 1);
	else
		ret = mSpDev->setCtrl(V4L2_CID_HFLIP, 0);
	if (needSetFlip & HAL_FLIP_V)
		ret = mSpDev->setCtrl(V4L2_CID_VFLIP, 1);
	else
		ret = mSpDev->setCtrl(V4L2_CID_VFLIP, 0);
	
	if(ret == 0)
		mFlip = setFlip;
	else {
		LOGW("Could not set flip, error %d", ret);
	}
	
	return ret;
}

int CamHwItf::queryBusInfo(unsigned char *busInfo)
{ 
	return mSpDev->queryBusInfo(busInfo);
}

void CamHwItf::transDrvMetaDataToHal
(
const void* drvMeta, 
struct HAL_Buffer_MetaData* halMeta
)
{
	return;
}

shared_ptr<CamHwItf> getCamHwItf(void )
{
shared_ptr<CamHwItf> instance;
#ifdef RK_ISP10
        instance = shared_ptr<CamHwItf>(new CamIsp10DevHwItf());
        mCamIsp10DevHwItf = static_cast<CamIsp10DevHwItf*>(instance.get());
				ALOGD("%s:%d",__func__,__LINE__);
        return instance;
#endif

#ifdef RK_ISP11
	instance = shared_ptr<CamHwItf>(new CamIsp11DevHwItf());
	mCamIsp11DevHwItf = static_cast<CamIsp11DevHwItf*>(instance.get());
	return instance;
#endif
}
