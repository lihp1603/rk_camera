#include <iostream>

#include "../HAL/include/CameraBuffer.h"
#include "../HAL/include/CamHwItf.h"
//#include "../HAL/include/CamIsp10DevHwItf.h"
#ifdef SUPPORT_ION
#include "../HAL/include/IonCameraBuffer.h"
#endif

#include "../HAL/include/camHalTrace.h"
#include "../HAL/include/StrmPUBase.h"

#include "../HAL/include/CameraIspTunning.h"

static void testTunningClass()
{

	//new ISP DEV
	ALOGD("construct ISP dev........");
        //shared_ptr<CamHwItf> testIspDev =  shared_ptr<CamHwItf>(new CamIsp10DevHwItf());//getCamHwItf();
        shared_ptr<CamHwItf> testIspDev =  getCamHwItf();
	ALOGD("init ISP dev......");
        if (testIspDev->initHw(0) == false)
                ALOGE("isp dev init error !\n");
#ifdef SUPPORT_ION //alloc buffers
    shared_ptr<IonCameraBufferAllocator> bufAlloc(new IonCameraBufferAllocator());
	CameraIspTunning* tunningInstance = CameraIspTunning::createInstance(testIspDev.get());
	if (tunningInstance) {
		 frm_info_t frmFmt;
		 if(tunningInstance->prepare(frmFmt,4,bufAlloc)) {
			if (tunningInstance->start()) {
				//getchar to stop
				LOGD("press any key to stop tuning:");
				getchar();	
				tunningInstance->stop();
			}
		 }
	} 
		
		
#endif
	//deinit HW
	ALOGD("deinit ISP dev......");
	testIspDev->deInitHw();
	//delete isp dev
	ALOGD("destruct ISP dev......");
	testIspDev.reset();
}

int main()
{
	ALOGD("dumpsys start !\n");
	testTunningClass();
	ALOGD("dumpsys end !\n");
	return 0;
}


