/******************************************************************************
 *
 * Copyright 2010, Dream Chip Technologies GmbH. All rights reserved.
 * No part of this work may be reproduced, modified, distributed, transmitted,
 * transcribed, or translated into any language or computer format, in any form
 * or by any means without written permission of:
 * Dream Chip Technologies GmbH, Steinriede 10, 30827 Garbsen / Berenbostel,
 * Germany
 *
 *****************************************************************************/
#ifndef __ADPF_H__
#define __ADPF_H__

/**
 * @file adpf.h
 *
 * @brief
 *
 *****************************************************************************/
/**
 * @page module_name_page Module Name
 * Describe here what this module does.
 *
 * For a detailed list of functions and implementation detail refer to:
 * - @ref module_name
 *
 * @defgroup ADPF Auto denoising pre-filter module
 * @{
 *
 */
#include <ebase/types.h>
#include <common/return_codes.h>
#include <cam_calibdb/cam_calibdb_api.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ADPF_MASK (1 << 0)
#define ADPF_STRENGTH_MASK (1 << 1)
#define ADPF_DENOISE_SHARP_LEVEL_MASK (1 << 2)



#define CAMERIC_DPF_MAX_NLF_COEFFS      17
#define CAMERIC_DPF_MAX_SPATIAL_COEFFS  6

/*****************************************************************************/
/**
 *          AdpfHandle_t
 *
 * @brief   ADPF Module instance handle
 *
 *****************************************************************************/
typedef struct AdpfContext_s *AdpfHandle_t;         /**< handle to ADPF context */



/*****************************************************************************/
/**
 * @brief   A structure/tupple to represent gain values for four (R,Gr,Gb,B)
 *          channels.
 *
 * @note    The gain values are represented as float numbers.
 */
/*****************************************************************************/
typedef struct AdpfGains_s
{
    float   fRed;                               /**< gain value for the red channel */
    float   fGreenR;                            /**< gain value for the green channel in red lines */
    float   fGreenB;                            /**< gain value for the green channel in blue lines */
    float   fBlue;                              /**< gain value for the blue channel */
} AdpfGains_t;

/*****************************************************************************/
/**
 *          AdpfInstanceConfig_t
 *
 * @brief   ADPF Module instance configuration structure
 *
 *****************************************************************************/
typedef struct AdpfInstanceConfig_s
{
    AdpfHandle_t            hAdpf;              /**< handle returned by AdpfInit() */
} AdpfInstanceConfig_t;

typedef enum CamerIcDpfNoiseLevelLookUpScale_e
{
    CAMERIC_NLL_SCALE_INVALID       = -1,        /**< lower border (only for an internal evaluation) */
    CAMERIC_NLL_SCALE_LINEAR        = 1,        /**< use a linear scaling */
    CAMERIC_NLL_SCALE_LOGARITHMIC   = 0,        /**< use a logarithmic scaling */
    CAMERIC_NLL_SCALE_MAX                       /**< upper border (only for an internal evaluation) */
} CamerIcDpfNoiseLevelLookUpScale_t;

/*****************************************************************************/
/**
 * @brief   This type defines the 
 */
/*****************************************************************************/
typedef struct CamerIcDpfInvStrength_s
{
    uint8_t WeightR;
    uint8_t WeightG;
    uint8_t WeightB;
} CamerIcDpfInvStrength_t;

typedef struct CamerIcDpfNoiseLevelLookUp_s
{
    uint16_t                            NllCoeff[CAMERIC_DPF_MAX_NLF_COEFFS];   /**< Noise-Level-Lookup coefficients */
    CamerIcDpfNoiseLevelLookUpScale_t   xScale;                                 /**< type of x-axis (logarithmic or linear type) */
} CamerIcDpfNoiseLevelLookUp_t;

typedef struct CamerIcDpfSpatial_s
{
    uint8_t WeightCoeff[CAMERIC_DPF_MAX_SPATIAL_COEFFS];
} CamerIcDpfSpatial_t;

typedef enum CamerIcDpfGainUsage_e
{
    CAMERIC_DPF_GAIN_USAGE_INVALID       = 0,   /**< lower border (only for an internal evaluation) */
    CAMERIC_DPF_GAIN_USAGE_DISABLED      = 1,   /**< don't use any gains in preprocessing stage */
    CAMERIC_DPF_GAIN_USAGE_NF_GAINS      = 2,   /**< use only the noise function gains  from registers DPF_NF_GAIN_R, ... */
    CAMERIC_DPF_GAIN_USAGE_LSC_GAINS     = 3,   /**< use only the gains from LSC module */
    CAMERIC_DPF_GAIN_USAGE_NF_LSC_GAINS  = 4,   /**< use the moise function gains and the gains from LSC module */
    CAMERIC_DPF_GAIN_USAGE_AWB_GAINS     = 5,   /**< use only the gains from AWB module */
    CAMERIC_DPF_GAIN_USAGE_AWB_LSC_GAINS = 6,   /**< use the gains from AWB and LSC module */
    CAMERIC_DPF_GAIN_USAGE_MAX                  /**< upper border (only for an internal evaluation) */
} CamerIcDpfGainUsage_t;

typedef enum CamerIcDpfRedBlueFilterSize_e
{
    CAMERIC_DPF_RB_FILTERSIZE_INVALID   = -1,    /**< lower border (only for an internal evaluation) */
    CAMERIC_DPF_RB_FILTERSIZE_9x9       = 0,    /**< red and blue filter kernel size 9x9 (means 5x5 active pixel) */
    CAMERIC_DPF_RB_FILTERSIZE_13x9      = 1,    /**< red and blue filter kernel size 13x9 (means 7x5 active pixel) */
    CAMERIC_DPF_RB_FILTERSIZE_MAX               /**< upper border (only for an internal evaluation) */
} CamerIcDpfRedBlueFilterSize_t;

typedef struct CamerIcDpfConfig_s
{
    CamerIcDpfGainUsage_t           GainUsage;              /**< which gains shall be used in preprocessing stage of dpf module */

    CamerIcDpfRedBlueFilterSize_t   RBFilterSize;           /**< size of filter kernel for red/blue pixel */

    bool_t                          ProcessRedPixel;        /**< enable filter processing for red pixel */
    bool_t                          ProcessGreenRPixel;     /**< enable filter processing for green pixel in red lines */
    bool_t                          ProcessGreenBPixel;     /**< enable filter processing for green pixel in blue lines */
    bool_t                          ProcessBluePixel;       /**< enable filter processing for blux pixel */

    CamerIcDpfSpatial_t             SpatialG;               /**< spatial weights for green pixel */
    CamerIcDpfSpatial_t             SpatialRB;              /**< spatial weights for red/blue pixel */
} CamerIcDpfConfig_t;


/*****************************************************************************/
/**
 *          AdpfConfigType_t
 *
 * @brief   ADPF Configuration type
 *
 *****************************************************************************/
typedef enum AdpfConfigType_e
{
    ADPF_USE_CALIB_INVALID  = 0,                /**< invalid (could be zeroed memory) */
    ADPF_USE_CALIB_DATABASE = 1,
    ADPF_USE_DEFAULT_CONFIG = 2
} AdpfConfigType_t;

typedef enum AdpfMode_e {
	ADPF_MODE_INVALID = 0,
	ADPF_MODE_CONTROL_BY_GAIN = 1,
	ADPF_MODE_CONTROL_EXT = 2
} AdpfMode_t;


/*****************************************************************************/
/**
 *          AdpfConfig_t
 *
 * @brief   ADPF Module configuration structure
 *
 *****************************************************************************/
typedef struct AdpfConfig_s
{
	float  fSensorGain;        /**< initial sensor gain */
	AdpfConfigType_t type;               /**< configuration type */
	AdpfMode_t mode;
	union AdpfConfigData_u
	{
		struct AdpfDefaultConfig_s
		{
			uint32_t                SigmaGreen;         /**< sigma value for green pixel */
			uint32_t                SigmaRedBlue;       /**< sigma value for red/blue pixel */
			float                   fGradient;          /**< gradient value for dynamic strength calculation */
			float                   fOffset;            /**< offset value for dynamic strength calculation */
			float                   fMin;               /**< upper bound for dynamic strength calculation */
			float                   fDiv;               /**< division factor for dynamic strength calculation */
			AdpfGains_t             NfGains;            /**< noise function gains */
		} def;

		struct AdpfDatabaseConfig_s
		{
			uint16_t                width;              /**< picture width */
			uint16_t                height;             /**< picture height */
			uint16_t                framerate;          /**< frame rate */
			CamCalibDbHandle_t      hCamCalibDb;        /**< calibration database handle */
		} db;
	} data;
	
	CamerIcDpfInvStrength_t dynInvStrength;
} AdpfConfig_t;


typedef struct AdpfResult_s
{
	CamerIcDpfInvStrength_t DynInvStrength;

	CamerIcGains_t NfGains;
	CamerIcDpfNoiseLevelLookUp_t Nll; /**< noise level lookup */
	CamerIcDpfConfig_t DpfMode;

	unsigned int actives;

	CamerIcIspFltDeNoiseLevel_t denoise_level;
	CamerIcIspFltSharpeningLevel_t sharp_level;
} AdpfResult_t;

/*****************************************************************************/
/**
 * @brief   This function converts float based gains into CamerIC 4.8 fixpoint
 *          format.
 *
 * @param   pAdpfGains          gains in float based format
 * @param   pCamerIcGains       gains in fix point format
 *
 * @return                      Returns the result of the function call.
 * @retval  RET_SUCCESS         gains sucessfully converted
 * @retval  RET_NULL_POINTER    null pointer parameter
 *
 *****************************************************************************/
RESULT AdpfGains2CamerIcGains
(
    AdpfGains_t 	*pAdpfGains,
    CamerIcGains_t  *pCamerIcGains
);



/*****************************************************************************/
/**
 * @brief   This function converts CamerIC 4.8 fixpoint format into float
 *          based gains.
 *
 * @param   pCamerIcGains       gains in fix point format
 * @param   pAdpfGains          gains in float based format
 *
 * @return                      Returns the result of the function call.
 * @retval  RET_SUCCESS         gains sucessfully converted
 * @retval  RET_NULL_POINTER    null pointer parameter
 *
 *****************************************************************************/
RESULT CamerIcGains2AdpfGains
(
    CamerIcGains_t  *pCamerIcGains,
    AdpfGains_t     *pAdpfGains
);

/*****************************************************************************/
/**
 *          AdpfInit()
 *
 * @brief   This function initializes the Auto denoising pre-filter module
 *
 * @param   pInstConfig
 *
 * @return  Returns the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_INVALID_PARM
 * @retval  RET_OUTOFMEM
 *
 *****************************************************************************/
RESULT AdpfInit
(
    AdpfHandle_t *handle,
    AdpfConfig_t *pConfig
);



/*****************************************************************************/
/**
 *          AdpfRelease()
 *
 * @brief   The function releases/frees the Auto denoising pre-filter module
 *
 * @param   handle  Handle to ADPFM
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_FAILURE
 *
 *****************************************************************************/
RESULT AdpfRelease
(
    AdpfHandle_t handle
);



/*****************************************************************************/
/**
 *          AdpfConfigure()
 *
 * @brief   This function configures the Auto denoising pre-filter module
 *
 * @param   handle  Handle to ADPFM
 * @param   pConfig
 *
 * @return  Returns the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_WRONG_HANDLE
 * @retval  RET_INVALID_PARM
 * @retval  RET_WRONG_STATE
 *
 *****************************************************************************/
RESULT AdpfConfigure
(
    AdpfHandle_t handle,
    AdpfConfig_t *pConfig
);



/*****************************************************************************/
/**
 *          AdpfRun()
 *
 * @brief   The function calculates and adjusts a new DPF-setup regarding
 *          the current sensor-gain
 *
 * @param   handle  Handle to ADPFM
 *          gain    current sensor-gain
 *
 * @return  Return the result of the function call.
 * @retval  RET_SUCCESS
 * @retval  RET_FAILURE
 *
 *****************************************************************************/
RESULT AdpfRun
(
    AdpfHandle_t    handle,
    const float     gain
);

RESULT AdpfGetResult
(
	AdpfHandle_t    handle,
	AdpfResult_t	*result
);

#ifdef __cplusplus
}
#endif


/* @} ADPF */


#endif /* __ADPF_H__*/
