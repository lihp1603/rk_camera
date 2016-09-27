#
# RockChip Camera HAL 
#
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES +=\
	source/cam_types.cpp \
	source/CamThread.cpp \
	source/V4l2DevIoctr.cpp \
	source/CamHwItf.cpp \
	source/V4l2Isp10Ioctl.cpp \
	source/V4l2Isp11Ioctl.cpp \
	source/CamIsp10CtrItf.cpp \
	source/CamIsp11CtrItf.cpp \
	source/CamIsp1xCtrItf.cpp \
	source/camHalTrace.cpp\
	source/ProxyCameraBuffer.cpp\
	source/StreamPUBase.cpp\
	source/CamUSBDevHwItf.cpp\
	source/CamIsp10DevHwItf.cpp \
	source/CamIsp11DevHwItf.cpp \
	source/CameraIspTunning.cpp \
	source/CamCifDevHwItf.cpp

LOCAL_CFLAGS += -Wno-error=unused-function -Wno-array-bounds
LOCAL_CFLAGS += -DLINUX  -D_FILE_OFFSET_BITS=64 -DHAS_STDINT_H -DENABLE_ASSERT
LOCAL_CPPFLAGS += -std=c++11
LOCAL_CPPFLAGS += -D_GLIBCXX_USE_C99=1 -DLINUX  -DENABLE_ASSERT

LOCAL_SHARED_LIBRARIES :=

ifeq ($(IS_ANDROID_OS),true)
LOCAL_CPPFLAGS += -DANDROID_OS
LOCAL_C_INCLUDES += external/stlport/stlport bionic/ bionic/libstdc++/include system/core/libion/include/ \
		    system/core/include

LOCAL_SHARED_LIBRARIES += libcutils 
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
else
LOCAL_SHARED_LIBRARIES += libpthread
endif

ifeq ($(IS_NEED_SHARED_PTR),true)
LOCAL_CPPFLAGS += -D ANDROID_SHARED_PTR
endif

ifeq ($(IS_RK_ISP10),true)
LOCAL_CPPFLAGS += -D RK_ISP10
else
LOCAL_CPPFLAGS += -D RK_ISP11
endif


ifeq ($(IS_SUPPORT_ION),true)
LOCAL_SRC_FILES += source/IonCameraBuffer.cpp
endif

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/include\
	$(LOCAL_PATH)/include_private\
        $(LOCAL_PATH)/../include \
	$(LOCAL_PATH)/../include/linux \
			

LOCAL_STATIC_LIBRARIES := \
			  libisp_camera_engine \
			  libisp_aaa_af libisp_aaa_aec libisp_aaa_awb libisp_aaa_adpf\
			  libisp_calibdb libisp_cam_calibdb libtinyxml2 libexpat\
              		  libisp_ebase libisp_oslayer\

ifeq ($(IS_SUPPORT_ION),true)
LOCAL_SHARED_LIBRARIES += libion
endif

ifeq ($(IS_NEED_LINK_STLPORT),true)
LOCAL_SHARED_LIBRARIES += libstlport
endif


LOCAL_MODULE:= libcam_hal

#build dynamic lib
include $(BUILD_SHARED_LIBRARY)

