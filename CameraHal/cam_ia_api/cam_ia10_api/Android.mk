#ifeq ($(IS_CAM_IA10_API),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(IS_RK_ISP10),true)
LOCAL_CPPFLAGS += -D RK_ISP10
else
LOCAL_CPPFLAGS += -D RK_ISP11
endif

LOCAL_SRC_FILES:= \
	cam_ia10_engine.cpp \
	cam_ia10_engine_isp_modules.cpp
LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../include/ \
    $(LOCAL_PATH)/../tinyxml2

LOCAL_CFLAGS += -std=c99 -Wno-error=unused-function -Wno-array-bounds
LOCAL_CFLAGS += -DLINUX  -D_FILE_OFFSET_BITS=64 -DHAS_STDINT_H -DENABLE_ASSERT
LOCAL_CPPFLAGS += -std=c++11
LOCAL_CPPFLAGS += -D_GLIBCXX_USE_C99=1 -DLINUX  -DENABLE_ASSERT
LOCAL_STATIC_LIBRARIES := libisp_ebase libisp_oslayer libisp_calibdb libisp_aaa_af

LOCAL_MODULE:= libisp_camera_engine

LOCAL_MODULE_TAGS:= optional
include $(BUILD_STATIC_LIBRARY)

#endif
