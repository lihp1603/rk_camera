#ifndef _CAM_USB_DEV_HW_ITF_IMC_H_
#define _CAM_USB_DEV_HW_ITF_IMC_H_
#include "CamHwItf.h"
#include "ProxyCameraBuffer.h"

using namespace std;


#define CAMERAHAL_VIDEODEV_NONBLOCK

class CamUSBDevHwItf: public CamHwItf
{
public:
	CamUSBDevHwItf(void);
	virtual ~CamUSBDevHwItf(void);
	//if it is a USB device
	virtual shared_ptr<CamHwItf::PathBase> getPath(enum CamHwItf::PATHID id);
	virtual bool initHw(int inputId);
	virtual void deInitHw();
	
	class Path: public CamHwItf::PathBase
	{
		friend class CamUSBDevHwItf;
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
		Path(V4L2DevIoctr *camDev, PATHID pathID, unsigned long dequeueTimeout = 1000);
		virtual ~Path(void);
	private:
		shared_ptr<ProxyCameraBufferAllocator> mUSBBufAllocator;
	};
};

#endif
