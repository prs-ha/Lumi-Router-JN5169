
    
  
/**
 * @file  app_zcl_task.c
 * @brief ZCL Interface
 */

#include <jendefs.h>
#include <string.h>

/* Generated */
#include "pdum_gen.h"
#include "zps_gen.h"

/* Application */
#include "app_green_power.h"
#include "app_main.h"
#include "app_reporting.h"
#include "app_zcl_task.h"
#include "app_serial_commands.h"
#include "zcl_options.h"

/* SDK JN-SW-4170 */
#include "Basic.h"
#include "DeviceTemperatureConfiguration.h"
#include "GreenPower.h"
#include "Identify.h"

/* RGB Light */
#include "OnOff.h"
#include "LevelControl.h"
#include "ColourControl.h"

#include "ZTimer.h"
#include "dbg.h"
#include "zcl.h"

#ifndef TRACE_ZCL
#define TRACE_ZCL FALSE
#endif

#define ZCL_TICK_TIME ZTIMER_TIME_SEC(1)

PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterCustomCommands(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterUpdate(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleConfigureReportingRecord(tsZCL_CallBackEvent *psEvent);
PRIVATE teZCL_Status APP_ZCL_eRegisterEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack, APP_tsLumiRouter *psDeviceInfo);
PRIVATE void APP_ZCL_vDeviceSpecific_Init(void);
PRIVATE uint8 APP_u8AppendDecimal(char *pcBuffer, uint8 u8Value);

PUBLIC APP_tsLumiRouter sLumiRouter;

/* RGB attribute control buffers */
PRIVATE uint8 au8RgbOnOffAttrBits[sizeof(tsCLD_OnOff)];
PRIVATE uint8 au8RgbLevelAttrBits[sizeof(tsCLD_LevelControl)];
PRIVATE uint8 au8RgbColourAttrBits[sizeof(tsCLD_ColourControl)];

/* Ensure Basic cluster strings fit the tsCLD_Basic buffers (JN-SW-4170, Basic.h). */
#define APP_CHECK_BASIC_STRING(name, length, field) \
    typedef char name[((length) <= sizeof(sLumiRouter.sBasicServerCluster.field)) ? 1 : -1]

APP_CHECK_BASIC_STRING(APP_tManufacturerNameSizeCheck, CLD_BAS_MANUF_NAME_SIZE, au8ManufacturerName);
APP_CHECK_BASIC_STRING(APP_tModelIdentifierSizeCheck, CLD_BAS_MODEL_ID_SIZE, au8ModelIdentifier);
APP_CHECK_BASIC_STRING(APP_tBuildDateSizeCheck, CLD_BAS_DATE_SIZE, au8DateCode);
APP_CHECK_BASIC_STRING(APP_tVersionSizeCheck, CLD_BAS_SW_BUILD_SIZE, au8SWBuildID);

#undef APP_CHECK_BASIC_STRING

/**
 * @brief Initialises ZCL, registers the endpoints, and starts the tick timers
 */
PUBLIC void APP_ZCL_vInitialise(void)
{
    teZCL_Status eZCL_Status;

    /* Initialise ZCL */
    eZCL_Status = eZCL_Initialise(&APP_ZCL_cbGeneralCallback, apduZCL);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Initialisation: Error status=%x\n", eZCL_Status);
    }

    /* Start the tick timer */
    ZTIMER_eStart(u8TimerTick, ZCL_TICK_TIME);

    /* Register Router EndPoint */
    eZCL_Status = APP_ZCL_eRegisterEndPoint(&APP_ZCL_cbEndpointCallback, &sLumiRouter);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Registration: Error status=%x\n", eZCL_Status);
    }

    /* Initialise Green Power Proxy Basic */
    eZCL_Status = APP_eInitGreenPower(&APP_ZCL_cbEndpointCallback);
    if (eZCL_Status != E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL, "ZCL Green Power Initialisation: Error status=%x\n", eZCL_Status);
    }

