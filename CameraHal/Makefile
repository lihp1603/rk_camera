#CROSS_COMPILE ?= $(CURDIR)/../../../../toolschain/usr/bin/arm-linux-
#CROSS_COMPILE ?= /usr/bin/arm-linux-gnueabihf-
#CROSS_COMPILE ?= 
include $(CURDIR)/productConfigs.mk
export CROSS_COMPILE
LOCAL_PATH:= $(CURDIR)
include $(LOCAL_PATH)/build_system/Makefile.rules
#include $(LOCAL_PATH)/productConfigs.mk

export  BUILD_OUPUT_EXTERNAL_LIBS
export  IS_ANDROID_OS
export  IS_NEED_SHARED_PTR
export  IS_NEED_COMPILE_STLPORT
export  IS_NEED_LINK_STLPORT
export  IS_NEED_COMPILE_TINYXML2
export  IS_NEED_COMPILE_EXPAT
export  IS_SUPPORT_ION
export  IS_BUILD_TEST_APP
export	IS_CAM_IA10_API
export	IS_RK_ISP10


SUBDIRS ?= 

ifeq ($(IS_NEED_COMPILE_STLPORT),true)
SUBDIRS += stlport
endif

ifeq ($(IS_NEED_COMPILE_TINYXML2),true)
SUBDIRS += tinyxml2
endif

ifeq ($(IS_NEED_COMPILE_EXPAT),true)
SUBDIRS += expat
endif
SUBDIRS += ebase oslayer cam_calibdb calib_xml cam_ia_api HAL 

ifeq ($(IS_BUILD_TEST_APP),true)
SUBDIRS += testApp
SUBDIRS += dumpsys
endif

export BUILD_EVERYTHING
export CLEAN_EVERYTHING

define BUILD_EVERYTHING
	@for subdir in $(SUBDIRS); \
	do \
	    echo "making $@ in $$subdir"; \
	    ( cd $$subdir && $(MAKE) -f Android.mk ) \
		|| exit 1; \
	done
endef

define CLEAN_EVERYTHING
	-rm -fr ./build
	-rm -f `find ./ -name *.o`
	-rm -f `find ./ -path "./libs" -prune -name *.a`
	-rm -f `find ./ -name *.so`
endef

.PHONY:all
all:
	cp ./libs/* ./build/lib/ 
	$(BUILD_EVERYTHING)

clean:
	$(CLEAN_EVERYTHING)
	
