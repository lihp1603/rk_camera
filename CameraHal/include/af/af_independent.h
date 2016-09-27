#ifndef __AF_INDEPENDENT_H__
#define __AF_INDEPENDENT_H__

#include <oslayer/oslayer.h>
#include <common/list.h>
#include <common/return_codes.h>
typedef void *IsiSensorHandle_t;

//------------------------------------------

RESULT IsiMdiFocusSet
(
    void*   handle,
    const uint32_t      AbsStep
);

inline RESULT IsiMdiFocusSet
(
    void*   handle,
    const uint32_t      AbsStep
) { return 0; };

RESULT IsiMdiFocusGet
(
    void*   handle,
    uint32_t            *pAbsStep
);

inline RESULT IsiMdiFocusGet
(
    void*   handle,
    uint32_t            *pAbsStep
) { return 0; };

RESULT IsiMdiInitMotoDrive
(
    void*   handle
);

inline RESULT IsiMdiInitMotoDrive
(
    void*   handle
) { return 0; };

RESULT IsiMdiSetupMotoDrive
(
    void*   handle,
    uint32_t            *pMaxStep
);

inline RESULT IsiMdiSetupMotoDrive
(
    void*   handle,
    uint32_t            *pMaxStep
) { return 0; };
//---------------------------------------
typedef enum CamerIcIspAfmWindowId_e
{
    CAMERIC_ISP_AFM_WINDOW_INVALID  = 0,    /**< lower border (only for an internal evaluation) */
    CAMERIC_ISP_AFM_WINDOW_A        = 1,    /**< Window A (1st window) */
    CAMERIC_ISP_AFM_WINDOW_B        = 2,    /**< Window B (2nd window) */
    CAMERIC_ISP_AFM_WINDOW_C        = 3,    /**< Window C (3rd window) */
    CAMERIC_ISP_AFM_WINDOW_MAX,             /**< upper border (only for an internal evaluation) */
} CamerIcIspAfmWindowId_t;

/******************************************************************************/
/**
 *          CamerIcEventCb_t
 *
 *  @brief  Event callback
 *
 *  This callback is used to signal something to the application software,
 *  e.g. an error or an information.
 *
 *  @return void
 *
 *****************************************************************************/
typedef struct CamerIcEventCb_s
{
    void                *pUserContext;  /**< user context */
} CamerIcEventCb_t;


/******************************************************************************/
/**
 * @struct  CamerIcAfmMeasuringResult_s
 *
 * @brief   A structure to represent a complete set of measuring values.
 *
 *****************************************************************************/
typedef struct CamerIcAfmMeasuringResult_s
{
    uint32_t    SharpnessA;         /**< sharpness of window A */
    uint32_t    SharpnessB;         /**< sharpness of window B */
    uint32_t    SharpnessC;         /**< sharpness of window C */

    uint32_t    LuminanceA;         /**< luminance of window A */
    uint32_t    LuminanceB;         /**< luminance of window B */
    uint32_t    LuminanceC;         /**< luminance of window C */

    uint32_t    PixelCntA;
    uint32_t    PixelCntB;
    uint32_t    PixelCntC;

    CamerIcWindow_t   WindowA;      /* ddl@rock-chips.com: v1.6.0 */
    CamerIcWindow_t   WindowB;
    CamerIcWindow_t   WindowC;
    
} CamerIcAfmMeasuringResult_t;

/*****************************************************************************/
/**
 * @brief   CamerIc ISP AF module internal driver context.
 *
 *****************************************************************************/
 typedef struct CamerIcIspAfmContext_s
{
    bool_t                          enabled;        /**< measuring enabled */
    CamerIcEventCb_t                EventCb;

	CamerIcWindow_t                 WindowA;		/**< measuring window A */
	uint32_t                        PixelCntA;
	bool_t                          EnabledWdwA;
	CamerIcWindow_t                 WindowB;        /**< measuring window B */
	uint32_t                        PixelCntB;
	bool_t                          EnabledWdwB;
	CamerIcWindow_t                 WindowC;        /**< measuring window C */
	uint32_t                        PixelCntC;
	bool_t                          EnabledWdwC;

	uint32_t                        Threshold;
	uint32_t                        lum_shift;
	uint32_t                        afm_shift;
	uint32_t                        MaxPixelCnt;

	CamerIcAfmMeasuringResult_t     MeasResult;
} CamerIcIspAfmContext_t;
/*
typedef struct CamerIcDrvContext_s
{
	CamerIcIspAfmContext_t          *pIspAfmContext;
} CamerIcDrvContext_t;

typedef struct CamerIcDrvContext_s  *CamerIcDrvHandle_t;
*/

/******************************************************************************
 * CamerIcIspAfmMeasuringWindowIsEnabled()
 *****************************************************************************/
bool_t CamerIcIspAfmMeasuringWindowIsEnabled
(
    const CamerIcIspAfmWindowId_t   WdwId
);
#endif
