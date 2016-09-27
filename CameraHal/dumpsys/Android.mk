#
# RockChip Camera HAL 
#
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES +=\
        dumpsys.cpp \

LOCAL_SHARED_LIBRARIES:=

ifeq ($(IS_ANDROID_OS),true)
LOCAL_CPPFLAGS += -DANDROID_OS
LOCAL_C_INCLUDES += external/stlport/stlport bionic/ bionic/libstdc++/include system/core/libion/include/ \
			system/core/include
LOCAL_SHARED_LIBRARIES += libcutils
endif


ifeq ($(IS_NEED_SHARED_PTR),true)
LOCAL_CPPFLAGS += -D ANDROID_SHARED_PTR
endif

ifeq ($(IS_SUPPORT_ION),true)
LOCAL_CPPFLAGS += -DSUPPORT_ION
endif

#should include ../inlclude after system/core/include
LOCAL_C_INCLUDES += \
        $(LOCAL_PATH)/include\
        $(LOCAL_PATH)/../include\

LOCAL_CFLAGS += -Wno-error=unused-function -Wno-array-bounds
LOCAL_CFLAGS += -DLINUX  -D_FILE_OFFSET_BITS=64 -DHAS_STDINT_H -DENABLE_ASSERT
LOCAL_CPPFLAGS += -std=c++11
LOCAL_CPPFLAGS +=  -DLINUX  -DENABLE_ASSERT
LOCAL_SHARED_LIBRARIES += \
		libcam_hal \

ifeq ($(IS_NEED_LINK_STLPORT),true)
LOCAL_SHARED_LIBRARIES += \
                libstlport 
endif

LOCAL_STATIC_LIBRARIES := \
	libisp_ebase libisp_oslayer\


LOCAL_MODULE:= dumpsys

include $(BUILD_EXECUTABLE)
