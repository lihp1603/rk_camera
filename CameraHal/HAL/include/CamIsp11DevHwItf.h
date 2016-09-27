#ifndef _CAM_ISP11_DEV_HW_ITF_IMC_H_
#define _CAM_ISP11_DEV_HW_ITF_IMC_H_
#include "CamHwItf.h"
#include "CamIspCtrItf.h"
using namespace std;

#define CAMERA_DEVICE_NAME              "/dev/video"
#define CAMERA_CAPTURE_DEV_NAME   "/dev/video4"
#define CAMERA_OVERLAY_DEV_NAME   "/dev/video2"
#define CAMERA_DMA_DEV_NAME   "/dev/video5"
#define CAMERA_ISP_DEV_NAME   "/dev/video3"
#define CAMERA_IQ_DIR			"/etc/cam_iq/"
#define CAMERA_IQ_DEFAULT	"/etc/cam_iq/cam_default.xml"

#define CAMERAHAL_VIDEODEV_NONBLOCK

class CamIsp11CtrItf;

class CamIsp11DevHwItf: public CamHwItf
{
public:
	CamIsp11DevHwItf(void);
	virtual ~CamIsp11DevHwItf(void);
	//derived interfaces from CamHwItf
	virtual shared_ptr<CamHwItf::PathBase> getPath(enum CamHwItf::PATHID id);
	virtual bool initHw(int inputId);
	virtual void deInitHw();

	//ISP dev  inerfaces
	virtual int setExposure(unsigned int exposure, unsigned int gain, unsigned int gain_percent);
	virtual int setFPSCtrl();
	virtual int setAutoAdjustFPS(bool on) {UNUSED_PARAM(on);return -1;}
	virtual bool configureISPModules(const void* config);
	
	class Path: public CamHwItf::PathBase
	{
	friend class CamIsp11DevHwItf;
	public:
		virtual bool prepare(
			frm_info_t &frmFmt,
			unsigned int numBuffers,
			CameraBufferAllocator &allocator,
			bool cached,
			unsigned int minNumBuffersQueued = 1);

		virtual bool prepare(
			frm_info_t &frmFmt,
			list<shared_ptr<CameraBuffer> > &bufPool,
			unsigned int numBuffers,
			unsigned int minNumBuffersQueued = 1);

		virtual void addBufferNotifier(NewCameraBufferReadyNotifier *bufferReadyNotifier);
		virtual bool removeBufferNotifer(NewCameraBufferReadyNotifier *bufferReadyNotifier);
		virtual void releaseBuffers(void);
		virtual bool start(void);
		virtual void stop(void);
		virtual bool releaseBufToOwener(weak_ptr<CameraBuffer> camBuf);
		Path(CamIsp11DevHwItf *camIsp, V4L2DevIoctr *camDev, PATHID pathID, unsigned long dequeueTimeout = 1000);
		virtual ~Path(void);

	private:
		CamIsp11DevHwItf *mCamIsp;

	};

	virtual int setWhiteBalance(HAL_WB_MODE wbMode);
	virtual int setAntiBandMode(enum HAL_AE_FLK_MODE flkMode);
	virtual int setAeBias(int aeBias);
private:
	virtual void transDrvMetaDataToHal(const void* drvMeta, struct HAL_Buffer_MetaData* halMeta);
	virtual int configIsp(struct isp_supplemental_sensor_mode_data *sensor_mode_data, bool enable);
	int configIsp_l(struct isp_supplemental_sensor_mode_data *sensor);
	unsigned int mExposureSequence;
	volatile int m_fps_percent;
	volatile int m_last_fps_percent;

	shared_ptr<CamIsp11CtrItf> mISPDev;
	CamIspCtrItf::Configuration mIspCfg;
	osMutex mApiLock;

	char mIqPath[64];
};

#endif

