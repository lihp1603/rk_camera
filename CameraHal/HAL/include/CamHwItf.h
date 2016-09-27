#ifndef _CAM_HW_ITF_H_
#define _CAM_HW_ITF_H_

#include <memory>
#include <vector>
#include <list>
#include "cam_types.h"
#include "CameraBuffer.h"
#include "CamThread.h"
//#include "V4L2DevIoctr.h"


using namespace std;

class NewCameraBufferReadyNotifier;
class V4L2DevIoctr;

class CamHwItf : virtual public enable_shared_from_this<CamHwItf> 
{
public:
	enum PATHID {
		MP,
		SP,
		DMA,
		Y12
	};
	enum Input {
		INP_CSI0,
		INP_CSI1,
		INP_CPI,
		INP_DMA,
		INP_DMA_IE,
		INP_DMA_SP,
	};
	
	CamHwItf(void);
	virtual ~CamHwItf(void){};
	//data path
	class PathBase:public ICameraBufferOwener,public enable_shared_from_this<PathBase>
	{
	friend class CamHwItf;
	public:
		enum State {
		    UNINITIALIZED,
		    PREPARED,
		    STREAMING
		};

		virtual bool prepare(
			frm_info_t &frmFmt,
			unsigned int numBuffers,
			CameraBufferAllocator &allocator,
			bool cached,
			unsigned int minNumBuffersQueued = 1) = 0;

		virtual bool prepare(
			frm_info_t &frmFmt,
			list<shared_ptr<CameraBuffer> > &bufPool,
			unsigned int numBuffers,
			unsigned int minNumBuffersQueued = 1) = 0;

		virtual void addBufferNotifier(NewCameraBufferReadyNotifier *bufferReadyNotifier) = 0;
		virtual bool removeBufferNotifer(NewCameraBufferReadyNotifier *bufferReadyNotifier) = 0;
		virtual bool setInput(Input input) const;
		virtual void releaseBuffers(void) = 0;
		virtual bool start(void) = 0;
		virtual void stop(void) = 0;
		virtual unsigned int getMinNumUndequeueableBuffers(void) const;
		virtual unsigned int getNumQueuedBuffers(void) const;
		virtual bool releaseBufToOwener(weak_ptr<CameraBuffer> camBuf) = 0;
		virtual ~PathBase(void);
	protected:
		PathBase(CamHwItf *camHw,V4L2DevIoctr *camDev, PATHID pathID, unsigned long dequeueTimeout = 1000);

		bool dequeueFunc(void);

		V4L2DevIoctr *mCamDev;
		const PATHID mPathID;
		unsigned long mDequeueTimeout;
		State mState;
		unsigned int mNumBuffers;
		unsigned int mNumBuffersQueued;
		mutable osMutex mNumBuffersQueuedLock;
		mutable osMutex mPathLock;
		mutable osMutex mBufLock;
		unsigned int mMinNumBuffersQueued;
		unsigned int mNumBuffersUndequeueable;
		osEvent mBufferQueued;
		mutable osMutex mNotifierLock;
		list<NewCameraBufferReadyNotifier*> mBufferReadyNotifierList;
		CameraBufferAllocator *mBufferAllocator;
		list<shared_ptr<CameraBuffer> > mBufferPool;
		unsigned int mSkipFrames;
		int mPathRefCnt;
		CamHwItf *mCamHw;
		class DequeueThread : public CamThread
		{
			public:
			    DequeueThread(PathBase *path):mPath(path){};
			    virtual bool threadLoop(void) {return mPath->dequeueFunc();};
			private:
			    PathBase *mPath;
		};
		shared_ptr<DequeueThread> mDequeueThread;
			
	};
	virtual shared_ptr<PathBase> getPath(enum PATHID id) {
		UNUSED_PARAM(id);
		return shared_ptr<PathBase>(NULL);
	}
	//controls 
	virtual bool initHw(int inputId) = 0;
	virtual void deInitHw() = 0;
	//3A controls:AE,AWB,AF
	//AE
	virtual int getSupportedAeModes(vector<enum HAL_AE_OPERATION_MODE> &aeModes);
	virtual int setAeMode(enum HAL_AE_OPERATION_MODE aeMode);
	virtual int getAeMode(enum HAL_AE_OPERATION_MODE &aeMode);
	virtual int setAeBias(int aeBias);
	virtual int getAeBias(int &curAeBias);
	virtual int getSupportedBiasRange(HAL_RANGES_t &range);
	virtual int setAbsExp(int exposure);
	virtual int getAbsExp(int &curExposure);
	virtual int getSupportedExpMeterModes(vector<enum HAL_AE_METERING_MODE> modes);
	virtual int setExposureMeterMode(enum HAL_AE_METERING_MODE aeMeterMode);
	virtual int getExposureMeterMode(enum HAL_AE_METERING_MODE &aeMeterMode);
	//fps,anti banding contols,related to ae control
	virtual int setFps(HAL_FPS_INFO_t fps);
	//for mode HAL_AE_OPERATION_MODE_AUTO,
	virtual int enableAe(bool aeEnable);
	virtual int setAntiBandMode(enum HAL_AE_FLK_MODE flkMode);
	virtual int getSupportedAntiBandModes(vector<enum HAL_AE_FLK_MODE> &flkModes);
	virtual int setExposure(unsigned int exposure, unsigned int gain, unsigned int gain_percent);
	//AWB
	virtual int getSupportedWbModes(vector<HAL_WB_MODE> &modes);
	virtual int setWhiteBalance(HAL_WB_MODE wbMode);
	virtual int getWhiteBalanceMode(HAL_WB_MODE &wbMode);
	//for mode HAL_WB_AUTO,
	virtual int enableAwb(bool awbEnable);
	//AF
	virtual int setFocusPos(int position);
	virtual int getFocusPos(int &position);
	virtual int getSupportedFocusModes(vector<enum HAL_AF_MODE> fcModes);
	virtual int setFocusMode(enum HAL_AF_MODE fcMode);
	virtual int getFocusMode(enum HAL_AF_MODE &fcMode);
	virtual int getAfStatus(enum HAL_AF_STATUS &afStatus);
	//for af algorithm ?
	virtual int enableAf(bool afEnable);
	//single AF
	virtual int trigggerAf(bool trigger);
	//3A lock
	virtual int getSupported3ALocks(vector<enum HAL_3A_LOCKS> &locks);
	virtual int set3ALocks(int locks);
	virtual int get3ALocks(int &curLocks);
	//fmts:format , size , fps
	virtual bool enumSensorFmts(vector<frm_info_t> &frmInfos);
	virtual bool enumSupportedFmts(vector<RK_FRMAE_FORMAT> &frmFmts);
	virtual bool enumSupportedSizes(RK_FRMAE_FORMAT frmFmt,vector<frm_size_t> &frmSizes);
	virtual bool enumSupportedFps(RK_FRMAE_FORMAT frmFmt,frm_size_t frmSize,vector<HAL_FPS_INFO_t> &fpsVec);
	virtual int  tryFormat(frm_info_t inFmt, frm_info_t &outFmt);
	//flash control
	virtual int getSupportedFlashModes(vector<enum HAL_FLASH_MODE> &flModes);
	virtual int setFlashLightMode(enum HAL_FLASH_MODE flMode,int intensity,int timeout);
	virtual int getFlashLightMode(enum HAL_FLASH_MODE &flMode);
	//virtual int getFlashStrobeStatus(bool &flStatus);
	//virtual int enableFlashCharge(bool enable);
	
	//miscellaneous controls:
	//color effect,brightness,contrast,hue,saturation,ISO,scene mode,zoom
	//color effect
	virtual int getSupportedImgEffects(vector<enum HAL_IAMGE_EFFECT> &imgEffs);
	virtual int setImageEffect(enum HAL_IAMGE_EFFECT image_effect);
	virtual int getImageEffect(enum HAL_IAMGE_EFFECT &image_effect);
	//scene
	virtual int getSupportedSceneModes(vector<enum HAL_SCENE_MODE> &sceneModes);
	virtual int setSceneMode(enum HAL_SCENE_MODE sceneMode);
	virtual int getSceneMode(enum HAL_SCENE_MODE &sceneMode);
	//ISO
	virtual int getSupportedISOModes(vector<enum HAL_ISO_MODE> &isoModes);
	virtual int setISOMode(enum HAL_ISO_MODE isoMode, int sens);
	virtual int getISOMode(enum HAL_ISO_MODE &isoMode );
	//zoom
	virtual int getSupportedZoomRange(HAL_RANGES_t &zoomRange);
	virtual int setZoom(int zoomVal);
	virtual int getZoom(int &zoomVal);
	//brightness
	virtual int getSupportedBtRange(HAL_RANGES_t &brightRange);
	virtual int setBrightness(int brightVal);
	virtual int getBrithtness(int &brightVal);
	//contrast
	virtual int getSupportedCtRange(HAL_RANGES_t &contrastRange);
	virtual int setContrast(int contrast);
	virtual int getContrast(int &contrast);
	//saturation
	virtual int getSupportedStRange(HAL_RANGES_t &saturationRange);
	virtual int setSaturation(int sat);
	virtual int getSaturation(int &sat);
	//hue
	virtual int getSupportedHueRange(HAL_RANGES_t &hueRange);
	virtual int setHue(int hue);
	virtual int getHue(int &hue);
	virtual int setJpegQuality(int jpeg_quality){UNUSED_PARAM(jpeg_quality);return -1;}
	//flip
	virtual int setFlip(int flip);
	int queryBusInfo(unsigned char *busInfo);

	//ISP configure
	virtual bool configureISPModules(const void* config){ UNUSED_PARAM(config);return false;};
	protected:
	virtual void transDrvMetaDataToHal(const void* drvMeta, struct HAL_Buffer_MetaData* halMeta);
	//device ,path
	shared_ptr<PathBase> mSp;
	shared_ptr<PathBase> mMp;
	shared_ptr<PathBase> mDMAPath;
	shared_ptr<PathBase> mY12Path;
	shared_ptr<V4L2DevIoctr> mSpDev;
	shared_ptr<V4L2DevIoctr> mMpDev;
	shared_ptr<V4L2DevIoctr> mDmaPathDev;
	shared_ptr<V4L2DevIoctr> mY12PathDev;
	int m_cam_fd_capture;
	int m_cam_fd_overlay;
	int m_cam_fd_dma;
	int m_cam_fd_y12;
	int mInputId;
	bool m_flag_init;
	//effect
	enum HAL_IAMGE_EFFECT m_image_effect;
	//awb
	enum HAL_WB_MODE m_wb_mode;
	//ae
	enum HAL_AE_OPERATION_MODE mCurAeMode;
	int mCurAeBias;
	int mCurAbsExp;
	enum HAL_AE_METERING_MODE mCurAeMeterMode;
	enum HAL_AE_FLK_MODE mFlkMode;
	//af
	int mLastLensPosition;
	enum HAL_AF_MODE mAfMode;
	//3A lock
	int m3ALocks;
	//flash
	enum HAL_FLASH_MODE mFlMode;
	//scene
	enum HAL_SCENE_MODE mSceneMode;
	//ISO
	enum HAL_ISO_MODE mIsoMode;
	//zoom
	int mZoom;
	//brightness
	int mBrightness;
	//contrast
	int mContrast;
	//saturation
	int mSaturation;
	//hue
	int mHue;
	//flip
	int mFlip;
	//jpeg
	int m_jpeg_quality;

};

class NewCameraBufferReadyNotifier
{
public:
	virtual bool bufferReady(weak_ptr<CameraBuffer> buffer, int status) = 0;
	virtual ~NewCameraBufferReadyNotifier(void) {}
	frm_info_t& getReqFmt(){return mReqFmt;}
	void setReqFmt(const frm_info_t &reqFmt){mReqFmt = reqFmt;}
private:
	frm_info_t mReqFmt;
};

shared_ptr<CamHwItf> getCamHwItf(void );

#endif

