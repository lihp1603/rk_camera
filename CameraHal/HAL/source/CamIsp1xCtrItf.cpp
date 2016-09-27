#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "CamIsp1xCtrItf.h"
#include "camHalTrace.h"

using namespace std;

#define CAMERA_ISP_DEV_NAME   "/dev/video1"
#define TUNNING_FILE_PATH	 "/data/tunning.xml"

#if 0
static bool HAL_AE_FLK_MODE_to_AecEcmFlickerPeriod_t(
	HAL_AE_FLK_MODE in, AecEcmFlickerPeriod_t *out)
{
	switch (in) {
	case HAL_AE_FLK_OFF:
		*out = AEC_EXPOSURE_CONVERSION_FLICKER_OFF;
		return true;
	case HAL_AE_FLK_50:
		*out = AEC_EXPOSURE_CONVERSION_FLICKER_100HZ;
		return true;
	case HAL_AE_FLK_60:
		*out = AEC_EXPOSURE_CONVERSION_FLICKER_120HZ;
		return true;
	default:
		return false;
	}
}

#endif

void CamIsp1xCtrItf::mapHalWinToIsp(
	uint16_t in_width,uint16_t in_height,
	uint16_t in_hOff,uint16_t in_vOff,
	uint16_t drvWidth,uint16_t drvHeight,
	uint16_t& out_width,uint16_t& out_height,
	uint16_t& out_hOff,uint16_t& out_vOff
	)
{
	out_hOff = in_hOff * drvWidth / HAL_WIN_REF_WIDTH;
	out_vOff = in_vOff* drvHeight / HAL_WIN_REF_HEIGHT;
	out_width = in_width * drvWidth / HAL_WIN_REF_WIDTH;
	out_height = in_height * drvHeight / HAL_WIN_REF_HEIGHT;
}


CamIsp1xCtrItf::CamIsp1xCtrItf():
	mInitialized(false)
{
	//ALOGD("%s: E", __func__);

	mStartCnt = 0;
	mStreaming = false;
	mIspFd = -1;
	memset(&mCamIA_DyCfg, 0x00, sizeof(struct CamIA10_DyCfg));
	osMutexInit(&mApiLock);
	
	//ALOGD("%s: x", __func__);
}
CamIsp1xCtrItf::~CamIsp1xCtrItf()
{
	//ALOGD("%s: E", __func__);

	deInit();
	osMutexDestroy(&mApiLock);
	
	//ALOGD("%s: x", __func__);
}

bool CamIsp1xCtrItf::init(const char* tuningFile,
		const char* ispDev,
		CamHwItf* camHwItf)
{
	return true;
}
bool CamIsp1xCtrItf::deInit()
{
	//ALOGD("%s: E", __func__);

	osMutexLock(&mApiLock);

	if(mIspFd >= 0) {
		int ret;
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (mStreaming) {
			ret = ioctl(mIspFd, VIDIOC_STREAMOFF, &type);
			if(ret < 0) {
			ALOGE("%s: Failed to stop stream", __func__);
		}
		}

		for(int i = 0; i < CAM_ISP_NUM_OF_STAT_BUFS; i++) {
			if(mIspStatBuf[i]) {
				munmap(mIspStatBuf[i], mIspStatBufSize);
				mIspStatBuf[i] = NULL;
			}
		}		
		
		if(mIspFd >= 0)
			close(mIspFd);

		mIspStatBufSize = 0;
		mIspFd = -1;
		mInitialized = false;
	}
	mCamIAEngine.reset();
	osMutexUnlock(&mApiLock);
	
	//ALOGD("%s: x", __func__);
    return true;

}
bool CamIsp1xCtrItf::configure(const Configuration& config)
{
	bool ret = true;

	osMutexLock(&mApiLock);
	//HAL_AE_FLK_MODE_to_AecEcmFlickerPeriod_t(
	//	config.aec_cfg.flk,
	//	&mCamIA_DyCfg.aec.flicker);

	mCamIA_DyCfg.aec_cfg = config.aec_cfg;
	if ((mCamIA_DyCfg.aec_cfg.win.right_width == 0) ||
		(mCamIA_DyCfg.aec_cfg.win.bottom_height == 0))
	{
		mCamIA_DyCfg.aec_cfg.win.right_width = 2048;
        mCamIA_DyCfg.aec_cfg.win.bottom_height = 2048;
        mCamIA_DyCfg.aec_cfg.win.left_hoff = 0;
        mCamIA_DyCfg.aec_cfg.win.top_voff= 0;
	}
	
	mCamIA_DyCfg.aaa_locks = config.aaa_locks;
	mCamIA_DyCfg.awb_cfg = config.awb_cfg;
	mCamIA_DyCfg.afc_cfg = config.afc_cfg;
	mCamIA_DyCfg.flash_mode = config.flash_mode;
	mCamIA_DyCfg.uc = config.uc;

	mCamIA_DyCfg.sensor_mode.isp_input_width =
		config.sensor_mode.isp_input_width;
	mCamIA_DyCfg.sensor_mode.isp_input_height =
		config.sensor_mode.isp_input_height;
	mCamIA_DyCfg.sensor_mode.horizontal_crop_offset =
		config.sensor_mode.horizontal_crop_offset;
	mCamIA_DyCfg.sensor_mode.vertical_crop_offset =
		config.sensor_mode.vertical_crop_offset;
	mCamIA_DyCfg.sensor_mode.cropped_image_width =
		config.sensor_mode.cropped_image_width;
	mCamIA_DyCfg.sensor_mode.cropped_image_height =
		config.sensor_mode.cropped_image_height;
	mCamIA_DyCfg.sensor_mode.pixel_clock_freq_mhz =
		config.sensor_mode.pixel_clock_freq_mhz;
	mCamIA_DyCfg.sensor_mode.pixel_periods_per_line =
		config.sensor_mode.pixel_periods_per_line;
	mCamIA_DyCfg.sensor_mode.line_periods_per_field =
		config.sensor_mode.line_periods_per_field;
	mCamIA_DyCfg.sensor_mode.sensor_output_height =
		config.sensor_mode.sensor_output_height;
	mCamIA_DyCfg.sensor_mode.fine_integration_time_min =
		config.sensor_mode.fine_integration_time_min;
	mCamIA_DyCfg.sensor_mode.fine_integration_time_max_margin =
		config.sensor_mode.fine_integration_time_max_margin;
	mCamIA_DyCfg.sensor_mode.coarse_integration_time_min =
		config.sensor_mode.coarse_integration_time_min;
	mCamIA_DyCfg.sensor_mode.coarse_integration_time_max_margin =
		config.sensor_mode.coarse_integration_time_max_margin;
	
	osMutexUnlock(&mApiLock);
	
	//cproc
	osMutexLock(&mApiLock);
	if ((config.cproc.contrast != mCamIA_DyCfg.cproc.contrast)
		||(config.cproc.hue != mCamIA_DyCfg.cproc.hue)
		||(config.cproc.brightness != mCamIA_DyCfg.cproc.brightness)
		||(config.cproc.saturation != mCamIA_DyCfg.cproc.saturation)
		||(config.cproc.sharpness != mCamIA_DyCfg.cproc.sharpness)) {
		struct HAL_ISP_cfg_s cfg ;
		struct HAL_ISP_cproc_cfg_s cproc_cfg;
		cproc_cfg.range = HAL_ISP_COLOR_RANGE_OUT_FULL_RANGE;
		cfg.updated_mask = 0;
		cproc_cfg.cproc.contrast = config.cproc.contrast;
		cproc_cfg.cproc.hue = config.cproc.hue;
		cproc_cfg.cproc.brightness = config.cproc.brightness;
		cproc_cfg.cproc.saturation = config.cproc.saturation;
		cproc_cfg.cproc.sharpness = config.cproc.sharpness;
		cfg.cproc_cfg = &cproc_cfg;
		cfg.updated_mask |= HAL_ISP_CPROC_MASK;
		cfg.enabled[HAL_ISP_CPROC_ID] = HAL_ISP_ACTIVE_SETTING;
		if ((config.cproc.contrast < 0.01) 
			&& (config.cproc.hue < 0.01)
			&& (config.cproc.hue == 0)
			&& (config.cproc.saturation < 0.01)
			&& (config.cproc.sharpness < 0.01))
			cfg.enabled[HAL_ISP_CPROC_ID] = HAL_ISP_ACTIVE_FALSE;
		mCamIA_DyCfg.cproc = config.cproc;
		osMutexUnlock(&mApiLock);
		configureISP(&cfg);
	} else
		osMutexUnlock(&mApiLock);

	osMutexLock(&mApiLock);
	if (config.ie_mode != mCamIA_DyCfg.ie_mode) {
		struct HAL_ISP_cfg_s cfg;
		struct HAL_ISP_ie_cfg_s ie_cfg;
		cfg.updated_mask = 0;
		ie_cfg.range = HAL_ISP_COLOR_RANGE_OUT_FULL_RANGE;
		ie_cfg.mode = config.ie_mode;
		cfg.updated_mask |= HAL_ISP_IE_MASK;
		cfg.enabled[HAL_ISP_IE_ID] = HAL_ISP_ACTIVE_SETTING;
		if (ie_cfg.mode == HAL_EFFECT_NONE)
			cfg.enabled[HAL_ISP_IE_ID] = HAL_ISP_ACTIVE_FALSE;
		mCamIA_DyCfg.ie_mode = config.ie_mode;
		osMutexUnlock(&mApiLock);
		configureISP(&cfg);
	}else
		osMutexUnlock(&mApiLock);
	
	return ret;
}

/* control ISP module directly*/
bool CamIsp1xCtrItf::configureISP(const void* config)
{

	osMutexLock(&mApiLock);
	struct HAL_ISP_cfg_s *cfg = (struct HAL_ISP_cfg_s *)config;
	bool_t ret = BOOL_TRUE;
	/* following configs may confilt with 3A algorithm */
	if (cfg->updated_mask & HAL_ISP_HST_MASK) {
		if (cfg->enabled[HAL_ISP_HST_ID] && cfg->hst_cfg) {
			mHstNeededUpdate = BOOL_TRUE;
			mHstEnabled = HAL_ISP_ACTIVE_SETTING;
			hst_cfg = *cfg->hst_cfg;
		} else if ( !cfg->enabled[HAL_ISP_HST_ID]) {
			mHstNeededUpdate = BOOL_TRUE;
			mHstEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if (cfg->enabled[HAL_ISP_HST_ID]) {
			mHstNeededUpdate = BOOL_TRUE;
			mHstEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mHstNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP hst !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_AEC_MASK) {
		if (cfg->enabled[HAL_ISP_AEC_ID] && cfg->aec_cfg) {
			mAecNeededUpdate = BOOL_TRUE;
			mAecEnabled = HAL_ISP_ACTIVE_SETTING;
			aec_cfg = *cfg->aec_cfg;
		} else if ( !cfg->enabled[HAL_ISP_AEC_ID]) {
			mAecNeededUpdate = BOOL_TRUE;
			mAecEnabled = HAL_ISP_ACTIVE_FALSE;
			if (cfg->aec_cfg) {
				aec_cfg = *cfg->aec_cfg;
			} else {
				aec_cfg.exp_time = 0.0f;
				aec_cfg.exp_gain = 0.0f;
			}
		} else if ( cfg->enabled[HAL_ISP_AEC_ID]) {
			mAecNeededUpdate = BOOL_TRUE;
			mAecEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mAecNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP aec !",__func__);
			goto config_end;
		}
	}
	
	if (cfg->updated_mask & HAL_ISP_LSC_MASK) {
		if (cfg->enabled[HAL_ISP_LSC_ID] && cfg->lsc_cfg) {
			mLscNeededUpdate = BOOL_TRUE;
			mLscEnabled = HAL_ISP_ACTIVE_SETTING;
			lsc_cfg = *cfg->lsc_cfg;
		} else if ( !cfg->enabled[HAL_ISP_LSC_ID]) {
			mLscNeededUpdate = BOOL_TRUE;
			mLscEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if (cfg->enabled[HAL_ISP_LSC_ID]) {
			mLscNeededUpdate = BOOL_TRUE;
			mLscEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mLscNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP lsc !",__func__);
			goto config_end;
		}
	}
	
	if (cfg->updated_mask & HAL_ISP_AWB_GAIN_MASK) {
		if (cfg->enabled[HAL_ISP_AWB_GAIN_ID] && cfg->awb_gain_cfg) {
			mAwbGainNeededUpdate = BOOL_TRUE;
			mAwbEnabled = HAL_ISP_ACTIVE_SETTING;
			awb_gain_cfg = *cfg->awb_gain_cfg;
		} else if ( !cfg->enabled[HAL_ISP_AWB_GAIN_ID]) {
			mAwbGainNeededUpdate = BOOL_TRUE;
			mAwbEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_AWB_GAIN_ID]) {
			mAwbGainNeededUpdate = BOOL_TRUE;
			mAwbEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mAwbGainNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP awb gain !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_BPC_MASK) {
		if (cfg->enabled[HAL_ISP_BPC_ID] && cfg->dpcc_cfg) {
			mDpccNeededUpdate = BOOL_TRUE;
			mDpccEnabled = HAL_ISP_ACTIVE_SETTING;
			dpcc_cfg = *cfg->dpcc_cfg;
		} else if ( !cfg->enabled[HAL_ISP_BPC_ID]) {
			mDpccNeededUpdate = BOOL_TRUE;
			mDpccEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_BPC_ID]) {
			mDpccNeededUpdate = BOOL_TRUE;
			mDpccEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mDpccNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP dpcc !",__func__);
			goto config_end;
		}
	}
	
	if (cfg->updated_mask & HAL_ISP_SDG_MASK) {
		if (cfg->enabled[HAL_ISP_SDG_ID] && cfg->sdg_cfg) {
			mSdgNeededUpdate = BOOL_TRUE;
			mSdgEnabled = HAL_ISP_ACTIVE_SETTING;
			sdg_cfg = *cfg->sdg_cfg;
		} else if ( !cfg->enabled[HAL_ISP_SDG_ID]) {
			mSdgNeededUpdate = BOOL_TRUE;
			mSdgEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_SDG_ID]) {
			mSdgNeededUpdate = BOOL_TRUE;
			mSdgEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mSdgNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP sdg !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_CTK_MASK) {
		if (cfg->enabled[HAL_ISP_CTK_ID] && cfg->ctk_cfg) {
			mCtkNeededUpdate = BOOL_TRUE;
			mCtkEnabled = HAL_ISP_ACTIVE_SETTING;
			ctk_cfg = *cfg->ctk_cfg;
		} else if ( !cfg->enabled[HAL_ISP_CTK_ID]) {
			mCtkNeededUpdate = BOOL_TRUE;
			mCtkEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_CTK_ID]) {
			mCtkNeededUpdate = BOOL_TRUE;
			mCtkEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mCtkNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP ctk !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_AWB_MEAS_MASK) {
		if (cfg->enabled[HAL_ISP_AWB_MEAS_ID] && cfg->awb_cfg) {
			mAwbMeNeededUpdate = BOOL_TRUE;
			mAwbMeEnabled = HAL_ISP_ACTIVE_SETTING;
			awb_cfg = *cfg->awb_cfg;
		} else if ( !cfg->enabled[HAL_ISP_AWB_MEAS_ID]) {
			mAwbMeNeededUpdate = BOOL_TRUE;
			mAwbMeEnabled = HAL_ISP_ACTIVE_FALSE;
			if (cfg->awb_cfg) {
					awb_cfg.illuIndex =  cfg->awb_cfg->illuIndex;
			} else {
					awb_cfg.illuIndex =  -1;
			}
		} else if ( cfg->enabled[HAL_ISP_AWB_MEAS_ID]) {
			mAwbMeNeededUpdate = BOOL_TRUE;
			mAwbMeEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mAwbMeNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP awb measure !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_AFC_MASK) {
		if (cfg->enabled[HAL_ISP_AFC_ID] && cfg->afc_cfg) {
			mAfcNeededUpdate = BOOL_TRUE;
			mAfcEnabled = HAL_ISP_ACTIVE_SETTING;
			afc_cfg = *cfg->afc_cfg;
		} else if ( !cfg->enabled[HAL_ISP_AFC_ID]) {
			mAfcNeededUpdate = BOOL_TRUE;
			mAfcEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_AFC_ID]) {
			mAfcNeededUpdate = BOOL_TRUE;
			mAfcEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mAfcNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP afc !",__func__);
			goto config_end;
		}
	}
	
	if (cfg->updated_mask & HAL_ISP_DPF_MASK) {
		if (cfg->enabled[HAL_ISP_DPF_ID] && cfg->dpf_cfg) {
			mDpfNeededUpdate = BOOL_TRUE;
			mDpfEnabled = HAL_ISP_ACTIVE_SETTING;
			dpf_cfg = *cfg->dpf_cfg;
		} else if ( !cfg->enabled[HAL_ISP_DPF_ID]) {
			mDpfNeededUpdate = BOOL_TRUE;
			mDpfEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_DPF_ID]) {
			mDpfNeededUpdate = BOOL_TRUE;
			mDpfEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mDpfNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP dpf !",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_DPF_STRENGTH_MASK) {
		if (cfg->enabled[HAL_ISP_DPF_STRENGTH_ID] && cfg->dpf_strength_cfg) {
			mDpfStrengthNeededUpdate = BOOL_TRUE;
			mDpfStrengthEnabled = HAL_ISP_ACTIVE_SETTING;
			dpf_strength_cfg = *cfg->dpf_strength_cfg;
		} else if ( !cfg->enabled[HAL_ISP_DPF_STRENGTH_ID]) {
			mDpfStrengthNeededUpdate = BOOL_TRUE;
			mDpfStrengthEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_DPF_STRENGTH_ID]) {
			mDpfStrengthNeededUpdate = BOOL_TRUE;
			mDpfStrengthEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mDpfStrengthNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP dpf strength!",__func__);
			goto config_end;
		}
	}

	/* following configs may confilt with user settings */

	if (cfg->updated_mask & HAL_ISP_CPROC_MASK) {
		if (cfg->enabled[HAL_ISP_CPROC_ID] && cfg->cproc_cfg) {
			mCprocNeededUpdate = BOOL_TRUE;
			mCprocEnabled = HAL_ISP_ACTIVE_SETTING;
			cproc_cfg = *cfg->cproc_cfg;
		} else if ( !cfg->enabled[HAL_ISP_CPROC_ID]) {
			mCprocNeededUpdate = BOOL_TRUE;
			mCprocEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_CPROC_ID]) {
			mCprocNeededUpdate = BOOL_TRUE;
			mCprocEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mCprocNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP cproc!",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_IE_MASK) {
		if (cfg->enabled[HAL_ISP_IE_ID] && cfg->ie_cfg) {
			mIeNeededUpdate = BOOL_TRUE;
			mIeEnabled = HAL_ISP_ACTIVE_SETTING;
			ie_cfg = *cfg->ie_cfg;
		} else if ( !cfg->enabled[HAL_ISP_IE_ID]) {
			mIeNeededUpdate = BOOL_TRUE;
			mIeEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_IE_ID]) {
			mIeNeededUpdate = BOOL_TRUE;
			mIeEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mIeNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP ie!",__func__);
			goto config_end;
		}
	}
	
	/* can config free*/	
	if (cfg->updated_mask & HAL_ISP_GOC_MASK) {
		if (cfg->enabled[HAL_ISP_GOC_ID] && cfg->goc_cfg) {
			mGocNeededUpdate = BOOL_TRUE;
			mGocEnabled = HAL_ISP_ACTIVE_SETTING;
			goc_cfg = *cfg->goc_cfg;
		} else if ( !cfg->enabled[HAL_ISP_GOC_ID]) {
			mGocNeededUpdate = BOOL_TRUE;
			mGocEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_GOC_ID]) {
			mGocNeededUpdate = BOOL_TRUE;
			mGocEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mGocNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP goc!",__func__);
			goto config_end;
		}
	}

	if (cfg->updated_mask & HAL_ISP_FLT_MASK) {
		if (cfg->enabled[HAL_ISP_FLT_ID] && cfg->flt_cfg) {
			mFltNeededUpdate = BOOL_TRUE;
			mFltEnabled = HAL_ISP_ACTIVE_SETTING;
			flt_cfg = *cfg->flt_cfg;
			ALOGE("%s:HAL_ISP_FLT_MASK HAL_ISP_ACTIVE_SETTING!",__func__);
		} else if ( !cfg->enabled[HAL_ISP_FLT_ID]) {
			mFltNeededUpdate = BOOL_TRUE;
			mFltEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_FLT_ID]) {
			mFltNeededUpdate = BOOL_TRUE;
			mFltEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mFltNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP flt!",__func__);
			goto config_end;
		}
	}
	
	if (cfg->updated_mask & HAL_ISP_BDM_MASK) {
		if (cfg->enabled[HAL_ISP_BDM_ID] && cfg->bdm_cfg) {
			mBdmNeededUpdate = BOOL_TRUE;
			mBdmEnabled = HAL_ISP_ACTIVE_SETTING;
			bdm_cfg = *cfg->bdm_cfg;
		} else if ( !cfg->enabled[HAL_ISP_BDM_ID]) {
			mBdmNeededUpdate = BOOL_TRUE;
			mBdmEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_BDM_ID]) {
			mBdmNeededUpdate = BOOL_TRUE;
			mBdmEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mBdmNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP bdm!",__func__);
			goto config_end;
		}
	} 

	if (cfg->updated_mask & HAL_ISP_BLS_MASK) {
		if (cfg->enabled[HAL_ISP_BLS_ID] && cfg->bls_cfg) {
			mBlsNeededUpdate = BOOL_TRUE;
			mBlsEnabled = HAL_ISP_ACTIVE_SETTING;
			bls_cfg = *cfg->bls_cfg;
		} else if ( !cfg->enabled[HAL_ISP_BLS_ID]) {
			mBlsNeededUpdate = BOOL_TRUE;
			mBlsEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_BLS_ID]) {
			mBlsNeededUpdate = BOOL_TRUE;
			mBlsEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mBlsNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config bls !",__func__);
			goto config_end;
		}
	} 
	
	if (cfg->updated_mask & HAL_ISP_WDR_MASK) {
		if (cfg->enabled[HAL_ISP_WDR_ID] && cfg->wdr_cfg) {
			mWdrNeededUpdate = BOOL_TRUE;
			mWdrEnabled = HAL_ISP_ACTIVE_SETTING;
			wdr_cfg = *cfg->wdr_cfg;
		} else if ( !cfg->enabled[HAL_ISP_WDR_ID]) {
			mWdrNeededUpdate = BOOL_TRUE;
			mWdrEnabled = HAL_ISP_ACTIVE_FALSE;
		} else if ( cfg->enabled[HAL_ISP_WDR_ID]) {
			mWdrNeededUpdate = BOOL_TRUE;
			mWdrEnabled = HAL_ISP_ACTIVE_DEFAULT;
		} else {
			mWdrNeededUpdate = BOOL_FALSE;
			ALOGE("%s:can't config ISP bdm!",__func__);
			goto config_end;
		}
	}

	/* should reconfig 3A algorithm ?*/
config_end:	
	osMutexUnlock(&mApiLock);
	return ret;
}

bool CamIsp1xCtrItf::start()
{
	bool ret = true;
	
	osMutexLock(&mApiLock);
	if (!mInitialized)
		goto end;
	if (++mStartCnt > 1)
		goto end;

	if(!startMeasurements()) {
		ALOGE("%s failed to start measurements", __func__);
		ret = false;
		goto end;
	 }
	
	mISP3AThread->run("ISP3ATh",OSLAYER_THREAD_PRIO_HIGH);
end:	
	osMutexUnlock(&mApiLock);
	return ret;
}
bool CamIsp1xCtrItf::stop()
{
	bool ret = true;
	
	osMutexLock(&mApiLock);
	if (!mInitialized)
		goto end;
	if (--mStartCnt)
		goto end;
	osMutexUnlock(&mApiLock);
	mISP3AThread->requestExitAndWait();
	osMutexLock(&mApiLock);
	stopMeasurements();

end:	
	osMutexUnlock(&mApiLock);
	return ret;
}
/*
bool CamIsp1xCtrItf::awbConfig(struct HAL_AwbCfg *cfg)
{
	return true;
}
bool CamIsp1xCtrItf::aecConfig(struct HAL_AecCfg *cfg)
{
	return true;
}
bool CamIsp1xCtrItf::afcConfig(struct HAL_AfcCfg *cfg)
{
	return true;
}
bool CamIsp1xCtrItf::ispFunLock(unsigned int fun_lock)
{
	return true;
}
bool CamIsp1xCtrItf::ispFunEnable(unsigned int fun_en)
{
	return true;
}
bool CamIsp1xCtrItf::ispFunDisable(unsigned int fun_dis)
{
	return true;
}
bool CamIsp1xCtrItf::getIspFunStats(unsigned int  *lock, unsigned int *enable)
{
	return true;
}
*/

bool CamIsp1xCtrItf::initISPStream(const char* ispDev)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer v4l2_buf;

	mIspFd = open(ispDev, O_RDWR |O_NONBLOCK);
        if(mIspFd < 0) {
		ALOGE("%s: Cannot open %s (error : %s)\n", 
			__func__,
			ispDev,
			strerror(errno));
		return false;
        }
	
	req.count = CAM_ISP_NUM_OF_STAT_BUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(mIspFd, VIDIOC_REQBUFS, &req) < 0) {
		ALOGE("%s: VIDIOC_REQBUFS failed, strerror: %s",
			__func__,
			strerror(errno));
		return false;
	}

	v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_buf.memory = V4L2_MEMORY_MMAP;
	for(int i = 0; i < req.count; i++) {		
		v4l2_buf.index = i;
		if(ioctl(mIspFd , VIDIOC_QUERYBUF, &v4l2_buf) < 0) {
			ALOGE("%s: VIDIOC_QUERYBUF failed\n", __func__);
			return false;
		}

		mIspStatBuf[i] = mmap(0,
			v4l2_buf.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			mIspFd,
			v4l2_buf.m.offset);
		if (mIspStatBuf[i] == MAP_FAILED) {
			ALOGE("%s mmap() failed\n",__func__);
			return false;
		}

		if (ioctl(mIspFd, VIDIOC_QBUF, &v4l2_buf) < 0) {
			ALOGE("QBUF failed index %d", v4l2_buf.index);
			return false;
		}
	}

	mIspStatBufSize = v4l2_buf.length;
	return true;
}

bool CamIsp1xCtrItf::getMeasurement(struct v4l2_buffer& v4l2_buf)
{
	int retrycount = 3, ret;
	struct pollfd fds[1];
	int timeout_ms = 3000;

	fds[0].fd = mIspFd;
	fds[0].events = POLLIN |POLLERR;

	while(retrycount > 0) {
		ret = poll(fds, 1, timeout_ms);
		if (ret <= 0) {
			ALOGE("%s: poll error, %s",
				__FUNCTION__,
				strerror(errno));
			return false;
		}

		if (fds[0].revents & POLLERR) {
			ALOGD("%s: POLLERR in isp node", __FUNCTION__);
			return false;
		}

		if (fds[0].revents & POLLIN) {
			v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			v4l2_buf.memory = V4L2_MEMORY_MMAP;

			if(ioctl(mIspFd, VIDIOC_DQBUF, &v4l2_buf) < 0) {
				ALOGD("%s: VIDIOC_DQBUF failed, retry count %d\n",
					__FUNCTION__,
					retrycount);
				retrycount--;
				continue;
			}
			TRACE_D(1,"%s:  VIDIOC_DQBUF v4l2_buf: %d",
				__func__,
				v4l2_buf.index);
			if (v4l2_buf.sequence == (uint32_t)-1) {
				ALOGD("%s: sequence=-1 qbuf: %d", v4l2_buf.index);
				releaseMeasurement(&v4l2_buf);
			}
			
			return true;
		}
	}
	return false;
}
bool CamIsp1xCtrItf::releaseMeasurement(struct v4l2_buffer* v4l2_buf)
{
	if(ioctl(mIspFd, VIDIOC_QBUF, v4l2_buf) < 0) {
		ALOGE("%s: QBUF failed",__func__);
		return false;
	}

	return true;

}
bool CamIsp1xCtrItf::stopMeasurements()
{
	bool ret = false;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(mIspFd, VIDIOC_STREAMOFF, &type) < 0) {
		ALOGE("%s: VIDIOC_STREAMON failed\n", __func__);
		return false;
	}

	mStreaming = false;	
	return ret;
}
bool CamIsp1xCtrItf::startMeasurements()
{
	int ret;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	//ALOGD("%s: mIspFd: %d",
	//	__func__,
	//	mIspFd);
	if (ret = ioctl(mIspFd, VIDIOC_STREAMON, &type) < 0) {
		ALOGE("%s: VIDIOC_STREAMON failed, %s\n", __func__,
			strerror(ret));
		return false;
	}

	mStreaming = true;
	return true;
}

bool CamIsp1xCtrItf::runISPManual(struct CamIA10_Results* ia_results, bool_t lock)
{
	struct HAL_ISP_cfg_s  manCfg = {0};

	if (lock)
		osMutexLock(&mApiLock);

	/* TODO: following may conflict with AEC, now update always to override AEC settings*/
	if (mHstNeededUpdate) {
		manCfg.enabled[HAL_ISP_HST_ID] = mHstEnabled;
		if (mHstEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by aec default
			manCfg.updated_mask &= ~HAL_ISP_HST_MASK;
			mHstNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.hst_cfg= &hst_cfg;
			manCfg.updated_mask |= HAL_ISP_HST_MASK;
		}
	}

	if (mAecNeededUpdate) {
		manCfg.enabled[HAL_ISP_AEC_ID] = mAecEnabled;
		if (mAecEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by aec default
			manCfg.updated_mask &= ~HAL_ISP_AEC_MASK;
			mAecNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.aec_cfg= &aec_cfg;
			manCfg.updated_mask |= HAL_ISP_AEC_MASK;
		}
	}

	/* TODO: following may conflict with AWB, now update always to override AWB settings*/
	if (mLscNeededUpdate) {
		manCfg.enabled[HAL_ISP_LSC_ID] = mLscEnabled;
		if (mLscEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by awb default
			manCfg.updated_mask &= ~HAL_ISP_LSC_MASK;
			mLscNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.lsc_cfg= &lsc_cfg;
			manCfg.updated_mask |= HAL_ISP_LSC_MASK;
		}
	}

	if (mAwbGainNeededUpdate) {
		manCfg.enabled[HAL_ISP_AWB_GAIN_ID] = mAwbEnabled;
		if (mAwbEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by awb default
			manCfg.updated_mask &= ~HAL_ISP_AWB_GAIN_MASK;
			mAwbGainNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.awb_gain_cfg= &awb_gain_cfg;
			manCfg.updated_mask |= HAL_ISP_AWB_GAIN_MASK;
		}
	}

	if (mAwbMeNeededUpdate) {
		manCfg.enabled[HAL_ISP_AWB_MEAS_ID] = mAwbMeEnabled;
		if (mAwbMeEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by awb default
			manCfg.updated_mask &= ~HAL_ISP_AWB_MEAS_MASK;
			mAwbMeNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.awb_cfg= &awb_cfg;
			manCfg.updated_mask |= HAL_ISP_AWB_MEAS_MASK;
			//if awb gain is set, awb measure should enable
			if ((mAwbMeEnabled == HAL_ISP_ACTIVE_FALSE) &&
				(mAwbEnabled == HAL_ISP_ACTIVE_SETTING)) {
				//controlled by awb default
				manCfg.updated_mask &= ~HAL_ISP_AWB_MEAS_ID;
				mAwbMeNeededUpdate = BOOL_FALSE;
				}
		}
	}

	if (mCtkNeededUpdate) {
		manCfg.enabled[HAL_ISP_CTK_ID] = mCtkEnabled;
		if (mCtkEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by awb default
			manCfg.updated_mask &= ~HAL_ISP_CTK_MASK;
			mCtkNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.ctk_cfg= &ctk_cfg;
			manCfg.updated_mask |= HAL_ISP_CTK_MASK;
		}
	}

	/* TODO: following may conflict with AWB, now update always to override AWB settings*/
	if (mDpfNeededUpdate) {
		manCfg.enabled[HAL_ISP_DPF_ID] = mDpfEnabled;
		if (mDpfEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by adpf default
			manCfg.updated_mask &= ~HAL_ISP_DPF_MASK;
			mDpfNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.dpf_cfg= &dpf_cfg;
			manCfg.updated_mask |= HAL_ISP_DPF_MASK;
		}
	}

	if (mDpfStrengthNeededUpdate) {
		manCfg.enabled[HAL_ISP_DPF_STRENGTH_ID] = mDpfStrengthEnabled;
		if (mDpfStrengthEnabled == HAL_ISP_ACTIVE_DEFAULT) {
			//controlled by adpf default
			manCfg.updated_mask &= ~HAL_ISP_DPF_STRENGTH_MASK;
			mDpfStrengthNeededUpdate = BOOL_FALSE;
		} else {
			manCfg.dpf_strength_cfg= &dpf_strength_cfg;
			manCfg.updated_mask |= HAL_ISP_DPF_STRENGTH_MASK;
		}
	}

	/* TODO: following may conflict with AFC, now update always to override AFC settings*/
	if (mAfcNeededUpdate) {
		manCfg.afc_cfg= &afc_cfg;
		manCfg.updated_mask |= HAL_ISP_AFC_MASK;
		manCfg.enabled[HAL_ISP_AFC_ID] = mAfcEnabled;
		//mAfcNeededUpdate= BOOL_FALSE;
	}	

	if (mBlsNeededUpdate) {
		manCfg.bls_cfg = &bls_cfg;
		manCfg.updated_mask |= HAL_ISP_BLS_MASK;
		manCfg.enabled[HAL_ISP_BLS_ID] = mBlsEnabled;
		mBlsNeededUpdate = BOOL_FALSE;
	} 

	if (mIeNeededUpdate) {
		manCfg.ie_cfg = &ie_cfg;
		manCfg.updated_mask |= HAL_ISP_IE_MASK;
		manCfg.enabled[HAL_ISP_IE_ID] = mIeEnabled;
		mIeNeededUpdate= BOOL_FALSE;
	} 

	if (mDpccNeededUpdate) {
		manCfg.dpcc_cfg = &dpcc_cfg;
		manCfg.updated_mask |= HAL_ISP_BPC_MASK;
		manCfg.enabled[HAL_ISP_BPC_ID] = mDpccEnabled;
		mDpccNeededUpdate = BOOL_FALSE;
	}

	if (mSdgNeededUpdate) {
		manCfg.sdg_cfg = &sdg_cfg;
		manCfg.updated_mask |= HAL_ISP_SDG_MASK;
		manCfg.enabled[HAL_ISP_SDG_ID] = mSdgEnabled;
		mSdgNeededUpdate = BOOL_FALSE;
	}
	if (mFltNeededUpdate) {
		manCfg.flt_cfg= &flt_cfg;
		manCfg.updated_mask |= HAL_ISP_FLT_MASK;
		manCfg.enabled[HAL_ISP_FLT_ID] = mFltEnabled;
		mFltNeededUpdate = BOOL_FALSE;
	}

	if (mBdmNeededUpdate) {
		manCfg.bdm_cfg= &bdm_cfg;
		manCfg.updated_mask |= HAL_ISP_BDM_MASK;
		manCfg.enabled[HAL_ISP_BDM_ID] = mBdmEnabled;
		mBdmNeededUpdate = BOOL_FALSE;
	}

	if (mGocNeededUpdate) {
		manCfg.goc_cfg= &goc_cfg;
		manCfg.updated_mask |= HAL_ISP_GOC_MASK;
		manCfg.enabled[HAL_ISP_GOC_ID] = mGocEnabled;
		mGocNeededUpdate = BOOL_FALSE;
	}

	if (mCprocNeededUpdate) {
		manCfg.cproc_cfg= &cproc_cfg;
		manCfg.updated_mask |= HAL_ISP_CPROC_MASK;
		manCfg.enabled[HAL_ISP_CPROC_ID] = mCprocEnabled;
		mCprocNeededUpdate= BOOL_FALSE;
	}

	if (mWdrNeededUpdate) {
		manCfg.wdr_cfg= &wdr_cfg;
		manCfg.updated_mask |= HAL_ISP_WDR_MASK;
		manCfg.enabled[HAL_ISP_WDR_ID] = mWdrEnabled;
		mWdrNeededUpdate= BOOL_FALSE;
	}

	if (lock)
		osMutexUnlock(&mApiLock);
	
	if (RET_SUCCESS != mCamIAEngine->runManISP(&manCfg,ia_results))
		return BOOL_FALSE;
	else
		return BOOL_TRUE;
}

void CamIsp1xCtrItf::transDrvMetaDataToHal
(
const void* drvMeta, 
struct HAL_Buffer_MetaData* halMeta
)
{
	return;
}
bool CamIsp1xCtrItf::runIA(struct CamIA10_DyCfg *ia_dcfg,
	struct CamIA10_Stats *ia_stats,
	struct CamIA10_Results *ia_results)
{
	if (ia_dcfg)
		mCamIAEngine->initDynamic(ia_dcfg);

	if (ia_stats) {
		mCamIAEngine->setStatistics(ia_stats);

		if (ia_stats->meas_type & CAMIA10_AEC_MASK) {
			mCamIAEngine->runAEC();
			mCamIAEngine->runADPF();
		}

		if (ia_stats->meas_type & CAMIA10_AWB_MEAS_MASK) {
			mCamIAEngine->runAWB();
		}

		if (ia_stats->meas_type & CAMIA10_AFC_MASK) {
			mCamIAEngine->runAF();
		}
	}
	
	if (ia_results) {
		ia_results->active = 0;
		if (mCamIAEngine->getAECResults(&ia_results->aec) == RET_SUCCESS) {
			ia_results->active |= CAMIA10_AEC_MASK;
			ia_results->hst.enabled = BOOL_TRUE;
			//copy aec hst result to struct hst, may be override by manual settings after
			ia_results->hst.mode = CAMERIC_ISP_HIST_MODE_RGB_COMBINED;
			ia_results->hst.Window.width = 
				ia_results->aec.meas_win.h_size;
			ia_results->hst.Window.height= 
				ia_results->aec.meas_win.v_size;
			ia_results->hst.Window.hOffset= 
				ia_results->aec.meas_win.h_offs;
			ia_results->hst.Window.vOffset= 
				ia_results->aec.meas_win.v_offs;
			ia_results->hst.StepSize = 
				ia_results->aec.stepSize;
			memcpy(ia_results->hst.Weights, 
				ia_results->aec.GridWeights, sizeof(ia_results->aec.GridWeights));
			ia_results->aec_enabled = BOOL_TRUE;
		}
		
		memset(&ia_results->awb,0,sizeof(ia_results->awb));
		if (mCamIAEngine->getAWBResults(&ia_results->awb) == RET_SUCCESS) {
			if (ia_results->awb.actives & AWB_RECONFIG_GAINS) 
				ia_results->active |= CAMIA10_AWB_GAIN_MASK;
			if ((ia_results->awb.actives & AWB_RECONFIG_CCMATRIX)
				|| (ia_results->awb.actives & AWB_RECONFIG_CCOFFSET))
				ia_results->active |= CAMIA10_CTK_MASK;
			if ((ia_results->awb.actives & AWB_RECONFIG_LSCMATRIX)
				|| (ia_results->awb.actives & AWB_RECONFIG_LSCSECTOR))
				ia_results->active |= CAMIA10_LSC_MASK;
			if ((ia_results->awb.actives & AWB_RECONFIG_MEASMODE)
				|| (ia_results->awb.actives & AWB_RECONFIG_MEASCFG)
				|| (ia_results->awb.actives & AWB_RECONFIG_AWBWIN))
				ia_results->active |= CAMIA10_AWB_MEAS_MASK;
			ia_results->awb_gains_enabled = BOOL_TRUE;
			ia_results->awb_meas_enabled = BOOL_TRUE;
			ia_results->lsc_enabled = BOOL_TRUE;
			ia_results->ctk_enabled = BOOL_TRUE;
		}

		if (mCamIAEngine->getADPFResults(&ia_results->adpf) == RET_SUCCESS) {
			if (ia_results->adpf.actives & ADPF_MASK)
				ia_results->active |= CAMIA10_DPF_MASK;
			if (ia_results->adpf.actives & ADPF_STRENGTH_MASK)
				ia_results->active |= CAMIA10_DPF_STRENGTH_MASK; 
			ia_results->adpf_enabled = BOOL_TRUE;
			ia_results->adpf_strength_enabled = BOOL_TRUE;

			#if 1
			if (ia_results->adpf.actives & ADPF_DENOISE_SHARP_LEVEL_MASK ){		
				flt_cfg.denoise_level = ia_results->adpf.denoise_level;
				flt_cfg.sharp_level = ia_results->adpf.sharp_level;
				mFltEnabled = HAL_ISP_ACTIVE_SETTING;
				mFltNeededUpdate = BOOL_TRUE;
				runISPManual(ia_results, BOOL_FALSE);
				ia_results->flt.enabled = BOOL_TRUE;
				ia_results->active |= CAMIA10_FLT_MASK;
			}
			else{
				ia_results->flt.enabled = BOOL_FALSE;
				ia_results->active |= CAMIA10_FLT_MASK;
			}
			#endif
			
		}
	}

}


