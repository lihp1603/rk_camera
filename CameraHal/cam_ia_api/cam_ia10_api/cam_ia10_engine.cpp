#include <ebase/types.h>
#include <ebase/trace.h>
#include <ebase/builtins.h>

#include <calib_xml/calibdb.h>
#include <utils/Log.h>

#include "cam_ia10_engine.h"
#include "cam_ia10_engine_isp_modules.h"

CamIA10Engine::CamIA10Engine():
	hCamCalibDb(NULL)
{
	memset(&dCfg, 0x00, sizeof(struct CamIA10_DyCfg));
	memset(&dCfgShd, 0x00, sizeof(struct CamIA10_DyCfg));
	memset(&lastAwbResult,0,sizeof(lastAwbResult));
	memset(&curAwbResult,0,sizeof(curAwbResult));
	memset(&curAecResult,0,sizeof(curAecResult));
	memset(&lastAecResult,0,sizeof(lastAecResult));
	memset(&adpfCfg,0,sizeof(adpfCfg));
	memset(&awbcfg,0,sizeof(awbcfg));
	memset(&aecCfg,0,sizeof(aecCfg));
	memset(&mStats,0,sizeof(mStats));
	hCamCalibDb = NULL;
	hAwb = 	NULL;
	hAdpf = NULL;
	hAf = NULL;
}

CamIA10Engine::~CamIA10Engine()
{
	//ALOGD("%s: E", __func__);

	if (hAwb)
	{
		AwbStop(hAwb);
		AwbRelease(hAwb);
		hAwb = NULL;
	}

	AecStop();
	AecRelease();
	if (hAdpf)
	{
		AdpfRelease(hAdpf);
		hAdpf = NULL;
	}
	 hCamCalibDb = NULL;
	 
	 //ALOGD("%s: x", __func__);
}

RESULT CamIA10Engine::initStatic
(
    char* aiqb_data_file
)
{
	RESULT result = RET_FAILURE;
	if (!hCamCalibDb) {
		if (calidb.CreateCalibDb(aiqb_data_file)) {
			ALOGD("load tunning file success.");
			hCamCalibDb = calidb.GetCalibDbHandle();
		} else {
			ALOGD("load tunning file failed..");
			goto init_fail;
		}
	}

	result = initAEC();
	if (result != RET_SUCCESS)
		goto init_fail;

	result = initAWB();
	if (result != RET_SUCCESS)
		goto init_fail;

	return RET_SUCCESS;
init_fail:
	return result;
}

RESULT CamIA10Engine::initDynamic(struct CamIA10_DyCfg *cfg)
{
	RESULT result = RET_SUCCESS;
	//init awb static
	if (!hAwb) {
		AwbInstanceConfig_t awbInstance;
		result = AwbInit(&awbInstance);
		if (result != RET_SUCCESS)
			goto initDynamic_end;
		else
			hAwb = awbInstance.hAwb;
		
		awbcfg.width= cfg->sensor_mode.isp_input_width;
		awbcfg.height = cfg->sensor_mode.isp_input_height;
		awbcfg.awbWin.h_offs = 0;
		awbcfg.awbWin.v_offs = 0;
		awbcfg.awbWin.h_size = HAL_WIN_REF_WIDTH;
		awbcfg.awbWin.v_size = HAL_WIN_REF_HEIGHT;

		result = AwbConfigure(hAwb,&awbcfg);
		if (result != RET_SUCCESS) {
			LOGE("%s:awb config failure!",__func__);
			AwbRelease(hAwb);
			hAwb = NULL;
			goto initDynamic_end;
			
		}
		//start awb
		result = AwbStart(hAwb,&awbcfg);
		if (result != RET_SUCCESS) {
			LOGE("%s:awb start failure!",__func__);
			AwbRelease(hAwb);
                        hAwb = NULL;
			goto initDynamic_end;
		}
		AwbRunningOutputResult_t outresult = {0};
		result =  AwbRun(hAwb,NULL,&outresult);
		if (result == RET_SUCCESS) {
			//convert result
			memset(&curAwbResult,0,sizeof(curAwbResult));
			convertAwbResult2Cameric(&outresult,&curAwbResult);
		} else {
			LOGE("%s:awb run failure!",__func__);
			AwbStop(hAwb);
			AwbRelease(hAwb);
                        hAwb = NULL;
                        goto initDynamic_end;
		}
	}
	else {
		if (cfg->awb_cfg.win.right_width && cfg->awb_cfg.win.bottom_height)
		{
			awbcfg.awbWin.h_offs = cfg->awb_cfg.win.left_hoff;
			awbcfg.awbWin.v_offs = cfg->awb_cfg.win.top_voff;
			awbcfg.awbWin.h_size = cfg->awb_cfg.win.right_width;
			awbcfg.awbWin.v_size = cfg->awb_cfg.win.bottom_height;
		} else {
			awbcfg.awbWin.h_offs = 0;
			awbcfg.awbWin.v_offs = 0;
			awbcfg.awbWin.h_size = HAL_WIN_REF_WIDTH;
			awbcfg.awbWin.v_size = HAL_WIN_REF_HEIGHT;
		}

		//mode change ?
		if (cfg->awb_cfg.mode != dCfg.awb_cfg.mode) {
			if (cfg->awb_cfg.mode != HAL_WB_AUTO) {
				char prfName[10];
				int i,no;
				CamIlluProfile_t *pIlluProfile = NULL;
				AwbStop(hAwb);
				awbcfg.Mode = AWB_MODE_MANUAL;
				//get index
				//A:3400k
				//D65:6500K artificial daylight
				//CWF:4150K cool whtie fluorescent
				//TL84: 4000K
				//D50: 5000k 
				//D75:7500K
				//HORIZON:2300K
				//SNOW :6800k
				//CANDLE:1850K
				if(cfg->awb_cfg.mode == HAL_WB_INCANDESCENT ) {
								strcpy(prfName, "A");
				} else if(cfg->awb_cfg.mode == HAL_WB_DAYLIGHT) {
								strcpy(prfName, "D65");
				} else if(cfg->awb_cfg.mode == HAL_WB_FLUORESCENT) {
								strcpy(prfName, "F11_TL84");
				} else if(cfg->awb_cfg.mode == HAL_WB_SUNSET) {
								strcpy(prfName, "HORIZON"); //not support now
				}else if(cfg->awb_cfg.mode == HAL_WB_CLOUDY_DAYLIGHT) {
								strcpy(prfName, "F2_CWF");
				} else if(cfg->awb_cfg.mode == HAL_WB_CANDLE) {
								strcpy(prfName, "U30"); //not support now
				} else
					LOGE("%s:not support this awb mode %d !",__func__,cfg->awb_cfg.mode);

				// get number of availabe illumination profiles from database
				result = CamCalibDbGetNoOfIlluminations( hCamCalibDb, &no );
				// run over all illumination profiles
				for ( i = 0; i < no; i++ )
				{
						// get an illumination profile from database
						result = CamCalibDbGetIlluminationByIdx( hCamCalibDb, i, &pIlluProfile );
						if(strstr(pIlluProfile->name, prfName))
						{
								awbcfg.idx = i;
								break;
						}
				}

				if ( i == no)
					LOGE("%s:can't find %s profile!",__func__,prfName);
				else
					AwbStart(hAwb,&awbcfg);
			} else {
				//from manual to auto
				AwbStop(hAwb);
				initAWB();
				AwbStart(hAwb,&awbcfg);
			}
		}

		//awb locks
		if ((cfg->aaa_locks & HAL_3A_LOCKS_WB)
			&& (dCfg.awb_cfg.mode == HAL_WB_AUTO)) {
			AwbTryLock(hAwb);
		} else if (dCfg.aaa_locks & HAL_3A_LOCKS_WB)
			AwbUnLock(hAwb);
	}

	if (!hAdpf) {		
		adpfCfg.data.db.width = cfg->sensor_mode.isp_input_width;
		adpfCfg.data.db.height = cfg->sensor_mode.isp_input_height;
		adpfCfg.data.db.hCamCalibDb  = hCamCalibDb;
		
		result = AdpfInit( &hAdpf, &adpfCfg);
	} else {
		result = AdpfConfigure(hAdpf,&adpfCfg);
		if (result != RET_SUCCESS)
			goto initDynamic_end;
	}
	dCfg = *cfg;

	struct HAL_AecCfg *set, *shd;
	set = &dCfg.aec_cfg;
	shd = &dCfgShd.aec_cfg;

	if ((set->win.left_hoff != shd->win.left_hoff) ||
		(set->win.top_voff != shd->win.top_voff) ||
		(set->win.right_width != shd->win.right_width) ||
		(set->win.bottom_height != shd->win.bottom_height) ||
		(set->meter_mode != shd->meter_mode) ||
		(set->mode != shd->mode) ||
		(set->flk != shd->flk) ||
		(set->ae_bias != shd->ae_bias)) {
		uint16_t step_width,step_height;
		//cifisp_histogram_mode mode = CIFISP_HISTOGRAM_MODE_RGB_COMBINED;
		cam_ia10_map_hal_win_to_isp(
			set->win.right_width,
			set->win.bottom_height,
			cfg->sensor_mode.isp_input_width,
			cfg->sensor_mode.isp_input_height,
			&step_width,
			&step_height
			);
		cam_ia10_isp_hst_update_stepSize(
			aecCfg.HistMode,
			aecCfg.GridWeights.uCoeff,
			step_width,
			step_height,
			&(aecCfg.StepSize));

		//ALOGD("aec set win:%dx%d",
		//	set->win.right_width,set->win.bottom_height);

		aecCfg.LinePeriodsPerField = dCfg.sensor_mode.line_periods_per_field;
		aecCfg.PixelClockFreqMHZ =  dCfg.sensor_mode.pixel_clock_freq_mhz;
		aecCfg.PixelPeriodsPerLine = dCfg.sensor_mode.pixel_periods_per_line;
		
		if (set->flk == HAL_AE_FLK_OFF)
			aecCfg.EcmFlickerSelect = AEC_EXPOSURE_CONVERSION_FLICKER_OFF;
		else if (set->flk == HAL_AE_FLK_50)
			aecCfg.EcmFlickerSelect = AEC_EXPOSURE_CONVERSION_FLICKER_100HZ;
		else if (set->flk == HAL_AE_FLK_60)
			aecCfg.EcmFlickerSelect = AEC_EXPOSURE_CONVERSION_FLICKER_120HZ;
		else
			aecCfg.EcmFlickerSelect = AEC_EXPOSURE_CONVERSION_FLICKER_100HZ;

		if (set->meter_mode == HAL_AE_METERING_MODE_CENTER) {
			#if 0
			unsigned char gridWeights[25];
			//cifisp_histogram_mode mode = CIFISP_HISTOGRAM_MODE_RGB_COMBINED;
			
			gridWeights[0] = 0x00;		//weight_00to40
			gridWeights[1] = 0x00;
			gridWeights[2] = 0x01;
			gridWeights[3] = 0x00;
			gridWeights[4] = 0x00;
			
			gridWeights[5] = 0x00;		//weight_01to41
			gridWeights[6] = 0x02;
			gridWeights[7] = 0x02;
			gridWeights[8] = 0x02;
			gridWeights[9] = 0x00;
			
			gridWeights[10] = 0x00; 	 //weight_02to42
			gridWeights[11] = 0x04;
			gridWeights[12] = 0x08;
			gridWeights[13] = 0x04;
			gridWeights[14] = 0x00;
			
			gridWeights[15] = 0x00; 	 //weight_03to43
			gridWeights[16] = 0x02;
			gridWeights[17] = 0x00;
			gridWeights[18] = 0x02;
			gridWeights[19] = 0x00;
			
			gridWeights[20] = 0x00; 	 //weight_04to44
			gridWeights[21] = 0x00;
			gridWeights[22] = 0x00;
			gridWeights[23] = 0x00;
			gridWeights[24] = 0x00;
			memcpy(aecCfg.GridWeights.uCoeff, gridWeights, sizeof(gridWeights));
			#else
			//do nothing ,just use the xml setting
			#endif
		} else if (set->meter_mode == HAL_AE_METERING_MODE_AVERAGE) {
			memset(aecCfg.GridWeights.uCoeff,0x01,sizeof(aecCfg.GridWeights.uCoeff));
		} else 
			ALOGE("%s:not support %d metering mode!",__func__,set->meter_mode);

		//set ae bias
		{
				CamCalibAecGlobal_t *pAecGlobal;
				CamCalibDbGetAecGlobal(hCamCalibDb, &pAecGlobal);			
				aecCfg.SetPoint =(pAecGlobal->SetPoint) +
					(set->ae_bias /100.0f) * MAX(10,pAecGlobal->SetPoint / (1 - pAecGlobal->ClmTolerance /100.0f ) /10.0f ) ;
		}
		
		AecStop();
		if ((set->mode != HAL_AE_OPERATION_MODE_MANUAL) && 
			!(cfg->aaa_locks & HAL_3A_LOCKS_EXPOSURE)){
			AecUpdateConfig(&aecCfg);
			AecStart();
			//get init result
			AecRun(NULL,NULL);
		}
		
		*shd = *set;
	}
initDynamic_end:	
	return result;
}

RESULT CamIA10Engine::setStatistics(struct CamIA10_Stats *stats)
{
	mStats = *stats;
	return RET_SUCCESS;
}

RESULT CamIA10Engine::initAWB()
{
	//init awbcfg
	const CamerIcAwbMeasuringConfig_t MeasConfig =
	{
			.MaxY 					= 200U,
			.RefCr_MaxR 		= 128U,
			.MinY_MaxG			=  30U,
			.RefCb_MaxB 		= 128U,
			.MaxCSum				=  20U,
			.MinC 					=  20U
	};
	awbcfg.framerate = 0;
	awbcfg.Mode = AWB_MODE_AUTO;
	awbcfg.idx = 0;
	awbcfg.damp = BOOL_TRUE;
	awbcfg.MeasMode = CAMERIC_ISP_AWB_MEASURING_MODE_YCBCR;
	
	awbcfg.fStableDeviation  = 0.1f;		 // 10 %
	awbcfg.fRestartDeviation = 0.2f;		 // 20 %
	
	awbcfg.MeasMode 				 = CAMERIC_ISP_AWB_MEASURING_MODE_YCBCR;
	awbcfg.MeasConfig 			 = MeasConfig;
	awbcfg.Flags						 = (AWB_WORKING_FLAG_USE_DAMPING | AWB_WORKING_FLAG_USE_CC_OFFSET); 	
	awbcfg.hCamCalibDb= hCamCalibDb;

	return RET_SUCCESS;
}

void CamIA10Engine::mapSensorExpToHal
(
	int sensorGain,
	int sensorInttime,
	float& halGain,
	float& halInttime
)
{

	typedef float gain_array_t[AEC_GAIN_RANG_MAX] ;
		
	gain_array_t gainRange = {
				  1,  1.9375, 16,  16, 1, 0, 15,
				  2,  3.875,  8,	0, 1, 16, 31,
				  4,  7.75,   4,  -32, 1, 48, 63,
				  8,  15.5,   2,  -96, 1, 112, 127};
	
	gain_array_t* pgainrange; 
		
	if (aecCfg.GainRange[0] < 1.0 && aecCfg.GainRange[1] < 1.0) {
					pgainrange = &gainRange;
	} else {
					pgainrange = &aecCfg.GainRange;
	}

	int revert_gain_array[] = {
		(int)(((*pgainrange)[0] * (*pgainrange)[2] - (*pgainrange)[3])/(*pgainrange)[4]+0.5),
		(int)(((*pgainrange)[1] * (*pgainrange)[2] - (*pgainrange)[3])/(*pgainrange)[4]+0.5),
		(int)(((*pgainrange)[7] * (*pgainrange)[9] - (*pgainrange)[10])/(*pgainrange)[11]+0.5),
		(int)(((*pgainrange)[8] * (*pgainrange)[9] - (*pgainrange)[10])/(*pgainrange)[11]+0.5),
		(int)(((*pgainrange)[14] * (*pgainrange)[16] - (*pgainrange)[17])/(*pgainrange)[18]+0.5),
		(int)(((*pgainrange)[15] * (*pgainrange)[16] - (*pgainrange)[17])/(*pgainrange)[18]+0.5),
		(int)(((*pgainrange)[21] * (*pgainrange)[23] - (*pgainrange)[24])/(*pgainrange)[25]+0.5),
		(int)(((*pgainrange)[22] * (*pgainrange)[23] - (*pgainrange)[24])/(*pgainrange)[25]+0.5),
	};


	// AG = (((C1 * analog gain - C0) / M0) + 0.5f
	float C1, C0, M0, minReg, maxReg,minHalGain,maxHalGain;
	float ag = sensorGain;
	
	if (ag >= revert_gain_array[0] && ag < revert_gain_array[1]) {
					C1 = (*pgainrange)[2];
					C0 = (*pgainrange)[3];
					M0 = (*pgainrange)[4];
					minReg = (*pgainrange)[5];
					maxReg = (*pgainrange)[6];
	} else if (ag >= revert_gain_array[1] && ag < revert_gain_array[3]) {
					C1 = (*pgainrange)[9];
					C0 = (*pgainrange)[10];
					M0 = (*pgainrange)[11];
					minReg = (*pgainrange)[12];
					maxReg = (*pgainrange)[13];
	} else if (ag >= revert_gain_array[3] && ag < revert_gain_array[5]) {
					C1 = (*pgainrange)[16];
					C0 = (*pgainrange)[17];
					M0 = (*pgainrange)[18];
					minReg = (*pgainrange)[19];
					maxReg = (*pgainrange)[20];
	} else if (ag >= revert_gain_array[5] && ag <= revert_gain_array[7]) {
					C1 = (*pgainrange)[23];
					C0 = (*pgainrange)[24];
					M0 = (*pgainrange)[25];
					minReg = (*pgainrange)[26];
					maxReg = (*pgainrange)[27];
	} else {
					ALOGE( "GAIN OUT OF RANGE: lasttime-gain: %d-%d", sensorInttime,sensorGain);
					C1 = 16;
					C0 = 0;
					M0 = 1;
					minReg = 16;
					maxReg = 255;
	}

	halGain = ((float)sensorGain * M0 + C0) / C1;
	minHalGain = ((float)minReg * M0 + C0) / C1;
	maxHalGain = ((float)maxReg * M0 + C0) / C1;
	if(halGain < minHalGain)
		halGain = minHalGain;
	if(halGain > maxHalGain)
		halGain = maxHalGain;

	#if 0
	halInttime = sensorInttime * aecCfg.PixelPeriodsPerLine /
								(aecCfg.PixelClockFreqMHZ * 1000000);
	#else
	float timeC0 = aecCfg.TimeFactor[0];
	float timeC1 = aecCfg.TimeFactor[1];
	float timeC2 = aecCfg.TimeFactor[2];
	float timeC3 = aecCfg.TimeFactor[3];

	halInttime = ((sensorInttime- timeC0*aecCfg.LinePeriodsPerField - timeC1)/timeC2 - timeC3) * 
		aecCfg.PixelPeriodsPerLine/(aecCfg.PixelClockFreqMHZ * 1000000);
	
	#endif
								
}
void CamIA10Engine::mapHalExpToSensor
(
	float hal_gain,
	float hal_time,
	int& sensor_gain,
	int& sensor_time
)
{
	typedef float gain_array_t[AEC_GAIN_RANG_MAX] ;
		
	gain_array_t gainRange = {
					  1,  1.9375, 16,  16, 1, 0, 15,
					  2,  3.875,  8,	0, 1, 16, 31,
					  4,  7.75,   4,  -32, 1, 48, 63,
					  8,  15.5,   2,  -96, 1, 112, 127};

	gain_array_t* pgainrange; 
	
	if (aecCfg.GainRange[0] < 1.0 && aecCfg.GainRange[1] < 1.0) {
					pgainrange = &gainRange;
	} else {
					pgainrange = &aecCfg.GainRange;
	}
	// AG = (((C1 * analog gain - C0) / M0) + 0.5f
	float C1, C0, M0, minReg, maxReg;
	float ag = hal_gain;
	
	if (ag >= (*pgainrange)[0] && ag < (*pgainrange)[1]) {
					C1 = (*pgainrange)[2];
					C0 = (*pgainrange)[3];
					M0 = (*pgainrange)[4];
					minReg = (*pgainrange)[5];
					maxReg = (*pgainrange)[6];
	} else if (ag >= (*pgainrange)[1] && ag < (*pgainrange)[8]) {
					C1 = (*pgainrange)[9];
					C0 = (*pgainrange)[10];
					M0 = (*pgainrange)[11];
					minReg = (*pgainrange)[12];
					maxReg = (*pgainrange)[13];
	} else if (ag >= (*pgainrange)[8] && ag < (*pgainrange)[15]) {
					C1 = (*pgainrange)[16];
					C0 = (*pgainrange)[17];
					M0 = (*pgainrange)[18];
					minReg = (*pgainrange)[19];
					maxReg = (*pgainrange)[20];
	} else if (ag >= (*pgainrange)[15] && ag <= (*pgainrange)[22]) {
					C1 = (*pgainrange)[23];
					C0 = (*pgainrange)[24];
					M0 = (*pgainrange)[25];
					minReg = (*pgainrange)[26];
					maxReg = (*pgainrange)[27];
	} else {
					ALOGE( "GAIN OUT OF RANGE: lasttime-gain: %f-%f", hal_time, hal_gain);
					C1 = 16;
					C0 = 0;
					M0 = 1;
					minReg = 16;
					maxReg = 255;
	}
	sensor_gain = (int)((C1 * hal_gain - C0) / M0 + 0.5f);
	if(sensor_gain < minReg)
		sensor_gain = minReg;
	if(sensor_gain > maxReg)
		sensor_gain = maxReg;

	#if 0
	sensor_time = (int)(hal_time * aecCfg.PixelClockFreqMHZ * 1000000 /
								aecCfg.PixelPeriodsPerLine + 0.5);
	#else
	float timeC0 = aecCfg.TimeFactor[0];
	float timeC1 = aecCfg.TimeFactor[1];
	float timeC2 = aecCfg.TimeFactor[2];
	float timeC3 = aecCfg.TimeFactor[3];
	sensor_time = (int)(timeC0*aecCfg.LinePeriodsPerField + timeC1 + 
		timeC2*((hal_time * aecCfg.PixelClockFreqMHZ * 1000000 / aecCfg.PixelPeriodsPerLine) + timeC3));
	#endif

}

RESULT CamIA10Engine::initAEC()
{
	RESULT ret = RET_FAILURE;

	CamCalibAecGlobal_t *pAecGlobal;
	ret = CamCalibDbGetAecGlobal(hCamCalibDb, &pAecGlobal);
	if (ret != RET_SUCCESS) {
		ALOGD("fail to get pAecGlobal, ret: %d", ret);
		return ret;
	}
	
    aecCfg.SetPoint       = pAecGlobal->SetPoint;
    aecCfg.ClmTolerance   = pAecGlobal->ClmTolerance;
    aecCfg.DampOverStill  = pAecGlobal->DampOverStill;
    aecCfg.DampUnderStill = pAecGlobal->DampUnderStill;
    aecCfg.DampOverVideo  = pAecGlobal->DampOverVideo;
    aecCfg.DampUnderVideo = pAecGlobal->DampUnderVideo;
	aecCfg.DampingMode    = AEC_DAMPING_MODE_STILL_IMAGE;
	aecCfg.SemMode        = AEC_SCENE_EVALUATION_DISABLED;

	memcpy(aecCfg.TimeFactor, pAecGlobal->TimeFactor, sizeof(pAecGlobal->TimeFactor));
	memcpy(aecCfg.GridWeights.uCoeff, pAecGlobal->GridWeights.uCoeff, sizeof(pAecGlobal->GridWeights.uCoeff));
	memcpy(aecCfg.GainRange, pAecGlobal->GainRange, sizeof(pAecGlobal->GainRange));
	memcpy(aecCfg.EcmTimeDot.fCoeff, pAecGlobal->EcmTimeDot.fCoeff, sizeof(pAecGlobal->EcmTimeDot.fCoeff));
	memcpy(aecCfg.EcmGainDot.fCoeff, pAecGlobal->EcmGainDot.fCoeff, sizeof(pAecGlobal->EcmGainDot.fCoeff));

	aecCfg.StepSize = 0;
	aecCfg.HistMode = CAMERIC_ISP_HIST_MODE_RGB_COMBINED;

	ret = AecInit(&aecCfg);

	return ret;
}

RESULT CamIA10Engine::runAEC()
{
	RESULT ret = RET_SUCCESS;

	int lastTime = lastAecResult.regIntegrationTime;
	int lastGain = lastAecResult.regGain;
	TRACE_D(1, "runAEC - cur time-gain: %d-%d, sensor: %d-%d", lastTime, dCfg.sensor_mode.exp_time, lastGain, dCfg.sensor_mode.gain);
	if (lastTime == 0 || lastGain == 0 
		|| dCfg.sensor_mode.exp_time == 0 || dCfg.sensor_mode.gain == 0
		|| (lastTime == dCfg.sensor_mode.exp_time && lastGain == dCfg.sensor_mode.gain))
	{
    	ret = AecRun(&mStats.aec, NULL);
	} 
	
    return ret;
}

RESULT CamIA10Engine::getAECResults(AecResult_t *result)
{
	struct HAL_AecCfg *set, *shd;
	
	set = &dCfg.aec_cfg;
	shd = &dCfgShd.aec_cfg;
	
	/*if ((set->win.h_offs != shd->win.h_offs) ||
		(set->win.v_offs != shd->win.v_offs) ||
		(set->win.h_size != shd->win.h_size) ||
		(set->win.v_size != shd->win.v_size)) {*/
	//if (true) {
	AecGetResults(result);
	
	if (lastAecResult.coarse_integration_time != result->coarse_integration_time
			|| lastAecResult.analog_gain_code_global != result->analog_gain_code_global) {
		result->actives |= CAMIA10_AEC_MASK;
		lastAecResult.coarse_integration_time = result->coarse_integration_time;
		lastAecResult.analog_gain_code_global = result->analog_gain_code_global;
		lastAecResult.regIntegrationTime = result->regIntegrationTime;
		lastAecResult.regGain = result->regGain;
		lastAecResult.gainFactor= result->gainFactor;
		lastAecResult.gainBias= result->gainBias;
		//*shd = *set;
	}
	
	result->actives |= CAMIA10_HST_MASK;
	result->meas_mode = AEC_MEASURING_MODE_1;
	result->meas_win.h_offs = set->win.left_hoff;
	result->meas_win.v_offs = set->win.top_voff;
	result->meas_win.h_size = set->win.right_width;
	result->meas_win.v_size = set->win.bottom_height;

	//ALOGD("set offset: %d-%d, size: %d-%d", set->win.left_hoff, set->win.top_voff, set->win.right_width, set->win.bottom_height);
	//ALOGD("ret offset: %d-%d, size: %d-%d", result->meas_win.h_offs, result->meas_win.v_offs, result->meas_win.h_size, result->meas_win.v_size);
	//ALOGD("sensor_mode size: %d-%d", dCfg.sensor_mode.isp_input_width, dCfg.sensor_mode.isp_input_height);
	//ALOGD("AEC time: %f, gain:%f", result->coarse_integration_time, result->analog_gain_code_global);
	
    return RET_SUCCESS;
}

void CamIA10Engine::convertAwbResult2Cameric
(
AwbRunningOutputResult_t *awbResult,
CamIA10_AWB_Result_t *awbCamicResult
)
{
	if ( !awbResult || !awbCamicResult)
		return;
	awbCamicResult->actives = awbResult->validParam;
	AwbGains2CamerIcGains(
		&awbResult->WbGains,
		&awbCamicResult->awbGains
	);

	AwbXtalk2CamerIcXtalk
	(
	    &awbResult->CcMatrix,
	    &awbCamicResult->CcMatrix
	);
	 
	AwbXTalkOffset2CamerIcXTalkOffset
	(
	    &awbResult->CcOffset,
	    &awbCamicResult->CcOffset
	);
	awbCamicResult->LscMatrixTable = awbResult->LscMatrixTable;
	awbCamicResult->SectorConfig   = awbResult->SectorConfig;
	awbCamicResult->MeasMode		=  awbResult->MeasMode;
	awbCamicResult->MeasConfig		=  awbResult->MeasConfig;
	awbCamicResult->awbWin			=  awbResult->awbWin;
	
}

void CamIA10Engine::updateAwbConfigs
(
CamIA10_AWB_Result_t *old,
CamIA10_AWB_Result_t *newCfg,
CamIA10_AWB_Result_t *update
)
{
	if (!old || !newCfg || !update)
		return;
	//LOGD("%s:%d,awb config actives,old->new:%d->%d \n",__func__,__LINE__,old->actives,newCfg->actives );
	if (newCfg->actives & AWB_RECONFIG_GAINS) {
		if ((newCfg->awbGains.Blue != old->awbGains.Blue)
			|| (newCfg->awbGains.Red != old->awbGains.Red)
			|| (newCfg->awbGains.GreenR != old->awbGains.GreenR)
			|| (newCfg->awbGains.GreenB != old->awbGains.GreenB)) {
			update->actives |= AWB_RECONFIG_GAINS;
			update->awbGains = newCfg->awbGains;
		}
	}

	//TODO:AWB_RECONFIG_CCMATRIX & AWB_RECONFIG_CCOFFSET should
	//update concurrently,now alogorithm ensure this
	if (newCfg->actives & AWB_RECONFIG_CCMATRIX) {
		int i = 0;
		
		for(;((i < 9) && 
			(newCfg->CcMatrix.Coeff[i] == old->CcMatrix.Coeff[i]))
			;i++);
		if (i != 9) {
			update->actives |= AWB_RECONFIG_CCMATRIX;
			update->CcMatrix = newCfg->CcMatrix;
		} else
			update->CcMatrix = newCfg->CcMatrix;
	} else
		update->CcMatrix = newCfg->CcMatrix;
	
	if (newCfg->actives & AWB_RECONFIG_CCOFFSET) {
		if ((newCfg->CcOffset.Blue) != (old->CcOffset.Blue)
			|| (newCfg->CcOffset.Red) != (old->CcOffset.Red)
			|| (newCfg->CcOffset.Green) != (old->CcOffset.Green)) {
			update->actives |= AWB_RECONFIG_CCOFFSET;
			update->CcOffset = newCfg->CcOffset;
		} else
			update->CcOffset = newCfg->CcOffset;
	} else
		update->CcOffset = newCfg->CcOffset;

	//TODO:AWB_RECONFIG_LSCMATRIX & AWB_RECONFIG_LSCSECTOR should
	//update concurrently,now alogorithm ensure this
	if (newCfg->actives & AWB_RECONFIG_LSCMATRIX) {
		update->actives |= AWB_RECONFIG_LSCMATRIX;
		update->LscMatrixTable = newCfg->LscMatrixTable;
	}  else
		update->LscMatrixTable = newCfg->LscMatrixTable;
	
	if (newCfg->actives & AWB_RECONFIG_LSCSECTOR) {
		update->actives |= AWB_RECONFIG_LSCSECTOR;
		update->SectorConfig = newCfg->SectorConfig;
	} else
		update->SectorConfig = newCfg->SectorConfig;

	//TODO:AWB_RECONFIG_MEASMODE & AWB_RECONFIG_MEASCFG & AWB_RECONFIG_AWBWIN
	//should update concurrently,now alogorithm ensure this
	if (newCfg->actives & AWB_RECONFIG_MEASMODE) {
		update->actives |= AWB_RECONFIG_MEASMODE;
		update->MeasMode = newCfg->MeasMode;
	} else
		update->MeasMode = newCfg->MeasMode;
	
	if (newCfg->actives & AWB_RECONFIG_MEASCFG) {
		update->actives |= AWB_RECONFIG_MEASCFG;
		update->MeasConfig = newCfg->MeasConfig;
	} else
		update->MeasConfig = newCfg->MeasConfig;
	
	if (newCfg->actives & AWB_RECONFIG_AWBWIN) {
		update->actives |= AWB_RECONFIG_AWBWIN;
		update->awbWin = newCfg->awbWin;
	} else
		update->awbWin = newCfg->awbWin;
	//LOGD("%s:%d,update awb config actives %d \n",__func__,__LINE__,update->actives );
}

RESULT CamIA10Engine::runAWB()
{
	//convert statics to awb algorithm
	AwbRunningInputParams_t MeasResult = {0};
	AwbRunningOutputResult_t result = {0};
	CamerIcAwbMeasure2AwbMeasure(&(mStats.awb),&(MeasResult.MesureResult));
	for (int i = 0; i < AWB_HIST_NUM_BINS; i++)
		MeasResult.HistBins[i] = mStats.aec.hist_bins[i];
	MeasResult.fGain = lastAecResult.analog_gain_code_global;
	MeasResult.fIntegrationTime = lastAecResult.coarse_integration_time;
	RESULT ret =  AwbRun(hAwb,&MeasResult,&result);
	if (ret == RET_SUCCESS) {
		//convert result
		memset(&curAwbResult,0,sizeof(curAwbResult));
		convertAwbResult2Cameric(&result,&curAwbResult);
	}
	return ret;
}

RESULT CamIA10Engine::getAWBResults(CamIA10_AWB_Result_t *result)
{
	updateAwbConfigs(&lastAwbResult,&curAwbResult,result);
	lastAwbResult = curAwbResult;
	return RET_SUCCESS;
}

RESULT CamIA10Engine::runAF()
{
	return RET_SUCCESS;
}

RESULT CamIA10Engine::getAFResults()
{
	return RET_SUCCESS;
}

RESULT CamIA10Engine::initADPF()
{
	RESULT result = RET_FAILURE;


	return result;
}

RESULT CamIA10Engine::runADPF() 
{
	RESULT ret = RET_FAILURE;
	
	ret = AdpfRun(hAdpf, lastAecResult.analog_gain_code_global);
	
	return RET_SUCCESS;
}

RESULT CamIA10Engine::getADPFResults(AdpfResult_t *result)
{
	RESULT ret = RET_FAILURE;
	ret = AdpfGetResult(hAdpf, result);
	
	return ret;
}


RESULT CamIA10Engine::runManISP(struct HAL_ISP_cfg_s* manCfg ,struct CamIA10_Results* result)
{
	RESULT ret = RET_SUCCESS;

	//may override other awb related modules, so need place it first.
	if (manCfg->updated_mask & HAL_ISP_AWB_MEAS_MASK) {
		CamerIcAwbMeasConfig_t awb_meas_result = {BOOL_FALSE,0};
		awb_meas_result.awb_meas_mode_result = &(result->awb.MeasMode);
		awb_meas_result.awb_meas_result = &(result->awb.MeasConfig);
		awb_meas_result.awb_win = &(result->awb.awbWin);
		ret = cam_ia10_isp_awb_meas_config
		(
			manCfg->enabled[HAL_ISP_AWB_MEAS_ID],
			manCfg->awb_cfg, 
			&(awb_meas_result)
		);

		if( (manCfg->awb_cfg) && (!manCfg->enabled[HAL_ISP_AWB_MEAS_ID])
			&& (manCfg->awb_cfg->illuIndex >= 0)) {
			//to get default LSC,CC,awb gain settings correspoding to illu
			AwbStop(hAwb);
			awbcfg.Mode = AWB_MODE_MANUAL;
			//ALOGD("%s:illu index %d",__func__,manCfg->awb_cfg->illuIndex);
			awbcfg.idx = manCfg->awb_cfg->illuIndex;
			AwbStart(hAwb,&awbcfg);
			runAWB();
			getAWBResults(&result->awb);
		}
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config AWB Meas failed !",__FUNCTION__);
		result->active |= CAMIA10_AWB_MEAS_MASK;
		result->awb_meas_enabled= awb_meas_result.enabled;
	}
	
	if (manCfg->updated_mask & HAL_ISP_BPC_MASK) {
		ret =  cam_ia10_isp_dpcc_config
		(
			manCfg->enabled[HAL_ISP_BPC_ID],
			manCfg->dpcc_cfg, 
			hCamCalibDb,
			dCfg.sensor_mode.isp_input_width,
			dCfg.sensor_mode.isp_input_height,
			&(result->dpcc)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config DPCC failed !",__FUNCTION__);
		result->active |= CAMIA10_BPC_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_BLS_MASK) {
		ret = cam_ia10_isp_bls_config
		(
			manCfg->enabled[HAL_ISP_BLS_ID],
			hCamCalibDb,
			dCfg.sensor_mode.isp_input_width,
			dCfg.sensor_mode.isp_input_height,
			manCfg->bls_cfg, 
			&(result->bls)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config BLS failed !",__FUNCTION__);
		result->active |= CAMIA10_BLS_MASK;
	}
	
	if (manCfg->updated_mask & HAL_ISP_SDG_MASK) {
		ret = cam_ia10_isp_sdg_config
		(
			manCfg->enabled[HAL_ISP_SDG_ID],
			manCfg->sdg_cfg, 
			&(result->sdg)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config SDG failed !",__FUNCTION__);
		result->active |= CAMIA10_SDG_MASK;
	}
	
	if (manCfg->updated_mask & HAL_ISP_HST_MASK) {
		ret = cam_ia10_isp_hst_config
		(
			manCfg->enabled[HAL_ISP_HST_ID],
			manCfg->hst_cfg, 
			dCfg.sensor_mode.isp_input_width,
			dCfg.sensor_mode.isp_input_height,
			&(result->hst)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config hst failed !",__FUNCTION__);
		result->active |= CAMIA10_HST_MASK;
		result->aec.actives |= CAMIA10_HST_MASK;
	}
	
	if (manCfg->updated_mask & HAL_ISP_LSC_MASK) {
		CamerIcLscConfig_t lsc_result = {BOOL_FALSE,0};
		lsc_result.lsc_result = &(result->awb.LscMatrixTable);
		lsc_result.lsc_seg_result = &(result->awb.SectorConfig);
		ret = cam_ia10_isp_lsc_config
		(
			manCfg->enabled[HAL_ISP_LSC_ID],
			manCfg->lsc_cfg, 
			&(lsc_result)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config LSC failed !",__FUNCTION__);
		result->active |= CAMIA10_LSC_MASK;
		result->lsc_enabled = lsc_result.enabled;
	}

	if (manCfg->updated_mask & HAL_ISP_AWB_GAIN_MASK) {
		CameraIcAwbGainConfig_t awb_result = {BOOL_FALSE,0};
		awb_result.awb_gain_result = &(result->awb.awbGains);
		ret = cam_ia10_isp_awb_gain_config
		(
			manCfg->enabled[HAL_ISP_AWB_GAIN_ID],
			manCfg->awb_gain_cfg, 
			&(awb_result)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config AWB Gain failed !",__FUNCTION__);
		result->active |= CAMIA10_AWB_GAIN_MASK;
		result->awb_gains_enabled= awb_result.enabled;
	}
	
	if (manCfg->updated_mask & HAL_ISP_FLT_MASK) {
		ret = cam_ia10_isp_flt_config
		(
			manCfg->enabled[HAL_ISP_FLT_ID],
			manCfg->flt_cfg, 
			&(result->flt)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config FLT failed !",__FUNCTION__);
		result->active |= CAMIA10_FLT_MASK;
	}
	
	if (manCfg->updated_mask & HAL_ISP_BDM_MASK) {
		ret = cam_ia10_isp_bdm_config
		(
			manCfg->enabled[HAL_ISP_BDM_ID],
			manCfg->bdm_cfg, 
			&(result->bdm)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config BDM failed !",__FUNCTION__);
		result->active |= CAMIA10_BDM_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_CTK_MASK) {
		CameraIcCtkConfig_t ctk_result = {BOOL_FALSE,0};
		ctk_result.ctk_matrix_result = &(result->awb.CcMatrix);
		ctk_result.ctk_offset_result = &(result->awb.CcOffset);
		ret = cam_ia10_isp_ctk_config
		(
			manCfg->enabled[HAL_ISP_CTK_ID],
			manCfg->ctk_cfg, 
			&(ctk_result)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config CTK failed !",__FUNCTION__);
		result->active |= CAMIA10_CTK_MASK;
		result->ctk_enabled= ctk_result.enabled;
	}

	if (manCfg->updated_mask & HAL_ISP_GOC_MASK) {
		ret = cam_ia10_isp_goc_config
		(
			hCamCalibDb,
			manCfg->enabled[HAL_ISP_GOC_ID],
			manCfg->goc_cfg, 
			&(result->goc)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config GOC failed !",__FUNCTION__);
		result->active |= CAMIA10_GOC_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_CPROC_MASK) {
		ret = cam_ia10_isp_cproc_config
		(
			manCfg->enabled[HAL_ISP_CPROC_ID],
			manCfg->cproc_cfg, 
			&(result->cproc)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config CPROC failed !",__FUNCTION__);
		result->active |= CAMIA10_CPROC_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_IE_MASK) {
		ret = cam_ia10_isp_ie_config
		(
			manCfg->enabled[HAL_ISP_IE_ID],
			manCfg->ie_cfg, 
			&(result->ie)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config IE failed !",__FUNCTION__);
		result->active |= CAMIA10_IE_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_AEC_MASK) {
		CameraIcAecConfig_t	aec_result = {BOOL_FALSE,0};
		aec_result.aec_meas_mode =  (int*)(&(result->aec.meas_mode));
		aec_result.meas_win = &(result->aec.meas_win);
		ret = cam_ia10_isp_aec_config
		(
			manCfg->enabled[HAL_ISP_AEC_ID],
			manCfg->aec_cfg, 
			&(aec_result)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config AEC Meas failed !",__FUNCTION__);
		result->active |= CAMIA10_AEC_MASK;
		result->aec_enabled= aec_result.enabled;
		
		if ((manCfg->aec_cfg) &&(!aec_result.enabled) && 
			((manCfg->aec_cfg->exp_time > 0.01) || (manCfg->aec_cfg->exp_gain > 0.01))) {
				mapHalExpToSensor
				(
					manCfg->aec_cfg->exp_gain,
					manCfg->aec_cfg->exp_time,
					result->aec.regGain,
					result->aec.regIntegrationTime
				);
				//FIXME: for some reason, kernel report error manual ae time and gain values to HAL
				//if aec is disabled , so here is just a workaround
				result->aec_enabled = BOOL_TRUE;
				result->aec.actives |= CAMIA10_AEC_MASK;
		}
		
	}

	/*TODOS*/
	if (manCfg->updated_mask & HAL_ISP_WDR_MASK) {
		ret = cam_ia10_isp_wdr_config
		(
		 	hCamCalibDb,
			manCfg->enabled[HAL_ISP_WDR_ID],
			manCfg->wdr_cfg, 
			&(result->wdr)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config WDR failed !",__FUNCTION__);
		result->active |= CAMIA10_WDR_MASK;
	}

	if (manCfg->updated_mask & HAL_ISP_DPF_MASK) {
		CameraIcDpfConfig_t dpfConfig;
		ret = cam_ia10_isp_dpf_config
		(
			manCfg->enabled[HAL_ISP_DPF_ID],
			manCfg->dpf_cfg, 
			&(dpfConfig)
		);
		
		if (ret != RET_SUCCESS)
			ALOGE("%s:config DPF failed !",__FUNCTION__);
		result->active |= CAMIA10_DPF_MASK;
		result->adpf_enabled = dpfConfig.enabled;
	}

	if (manCfg->updated_mask & HAL_ISP_DPF_STRENGTH_MASK) {
		CameraIcDpfStrengthConfig_t dpfStrengConfig;
		ret = cam_ia10_isp_dpf_strength_config
		(
			manCfg->enabled[HAL_ISP_DPF_STRENGTH_ID],
			manCfg->dpf_strength_cfg, 
			&(dpfStrengConfig)
		);
		
		result->adpf.DynInvStrength.WeightB = dpfStrengConfig.b;
		result->adpf.DynInvStrength.WeightG = dpfStrengConfig.g;
		result->adpf.DynInvStrength.WeightR = dpfStrengConfig.r;
		if (ret != RET_SUCCESS)
			ALOGE("%s:config DPF strength failed !",__FUNCTION__);
		result->active |= CAMIA10_DPF_STRENGTH_MASK;
		result->adpf_strength_enabled = dpfStrengConfig.enabled;
	}

	if (manCfg->updated_mask & HAL_ISP_AFC_MASK) {

	}

	return ret;
}

shared_ptr<CamIA10EngineItf> getCamIA10EngineItf(void)
{
	return shared_ptr<CamIA10EngineItf>(new CamIA10Engine());
}
