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
#include "app_tts_protocol.h"
#include "zcl_customcommand.h"

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

#define ZCL_TICK_TIME ZTIMER_TIME_MSEC(100)

PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterCustomCommands(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleClusterUpdate(tsZCL_CallBackEvent *psEvent);
PRIVATE void APP_ZCL_vHandleConfigureReportingRecord(tsZCL_CallBackEvent *psEvent);
PRIVATE teZCL_Status APP_ZCL_eRegisterEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack, APP_tsLumiRouter *psDeviceInfo);
PRIVATE void APP_ZCL_vDeviceSpecific_Init(void);
PRIVATE uint8 APP_u8AppendDecimal(char *pcBuffer, uint8 u8Value);

PUBLIC APP_tsLumiRouter sLumiRouter;

/* TTS experimental cluster: no attributes yet */
PRIVATE tsZCL_ClusterDefinition sTtsClusterDefinition = {
    .u16ClusterEnum = TTS_CLUSTER_ID,
    .bIsManufacturerSpecificCluster = TRUE,
    .u8ClusterControlFlags = 0,
    .u16NumberOfAttributes = 0,
    .psAttributeDefinition = NULL,
    .psSceneExtensionTable = NULL
};

PRIVATE uint8 u8TtsClusterData;
PRIVATE uint8 au8TtsAttributeControlBits[1];

PRIVATE teZCL_Status APP_TTS_eCommandHandler(
    ZPS_tsAfEvent *pZPSEvent,
    tsZCL_EndPointDefinition *psEndPointDefinition,
    tsZCL_ClusterInstance *psClusterInstance);

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

    APP_ZCL_vDeviceSpecific_Init();
}

/**
 * @brief Dispatches a Zigbee stack event to ZCL
 */
PUBLIC void APP_ZCL_vEventHandler(ZPS_tsAfEvent *psStackEvent)
{
    DBG_vPrintf(TRACE_ZCL, "ZCL Stack Event: type=%d\n", psStackEvent->eType);

    tsZCL_CallBackEvent sCallBackEvent = {.eEventType = E_ZCL_CBET_ZIGBEE_EVENT, .pZPSevent = psStackEvent};
    vZCL_EventHandler(&sCallBackEvent);
}

/**
 * @brief Dispatches the one-second ZCL tick and restarts the timer
 */

PUBLIC void APP_cbTimerZclTick(void *pvParam)
{
    static uint8 u8TickCount = 0;

    (void)pvParam;

    /* Update brightness transitions every 100 ms */
    eCLD_LevelControlUpdate(LUMIROUTER_APPLICATION_ENDPOINT);

    /* Keep the original ZCL one-second timer */
    u8TickCount++;

    if (u8TickCount >= 10) {
        tsZCL_CallBackEvent sCallBackEvent = {
            .eEventType = E_ZCL_CBET_TIMER
        };

        u8TickCount = 0;
        vZCL_EventHandler(&sCallBackEvent);
    }

    ZTIMER_eStart(u8TimerTick, ZCL_TICK_TIME);
}

/**
 * @brief General callback for ZCL events
 */
PRIVATE void APP_ZCL_cbGeneralCallback(tsZCL_CallBackEvent *psEvent)
{
    switch (psEvent->eEventType) {
    case E_ZCL_CBET_ERROR:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Error status=%x\n", psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_ZGP_DATA_IND_ERROR:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: GP data indication error status=%x\n", psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Unhandled event\n");
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "ZCL General Callback: Unexpected event type=%d\n", psEvent->eEventType);
        break;
    }
}

/**
 * @brief Endpoint specific callback for ZCL events
 */
PRIVATE void APP_ZCL_cbEndpointCallback(tsZCL_CallBackEvent *psEvent)
{
    switch (psEvent->eEventType) {
    case E_ZCL_CBET_READ_REQUEST:
        /* Use the current attribute values; do not refresh them before the read. */
        break;

    case E_ZCL_CBET_CHECK_ATTRIBUTE_RANGE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write range check cluster=%04x attribute=%04x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.u16AttributeEnum);
        break;

    case E_ZCL_CBET_WRITE_INDIVIDUAL_ATTRIBUTE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write attribute cluster=%04x attribute=%04x status=%02x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.u16AttributeEnum,
                    psEvent->uMessage.sIndividualAttributeResponse.eAttributeStatus);
        break;

    case E_ZCL_CBET_WRITE_ATTRIBUTES:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Write attributes request processed cluster=%04x status=%02x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum,
                    psEvent->eZCL_Status);
        break;

    case E_ZCL_CBET_REPORT_INDIVIDUAL_ATTRIBUTES_CONFIGURE:
        APP_ZCL_vHandleConfigureReportingRecord(psEvent);
        break;

    case E_ZCL_CBET_REPORT_ATTRIBUTES_CONFIGURE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting complete cluster=%04x\n",
                    psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum);
        break;

    case E_ZCL_CBET_REPORT_REQUEST:
        /* Use the current attribute values; do not refresh them before reporting. */
        break;

    case E_ZCL_CBET_CLUSTER_CUSTOM:
        APP_ZCL_vHandleClusterCustomCommands(psEvent);
        break;

    case E_ZCL_CBET_CLUSTER_UPDATE:
        APP_ZCL_vHandleClusterUpdate(psEvent);
        break;

    case E_ZCL_CBET_DEFAULT_RESPONSE:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Default response command=%02x status=%02x\n",
                    psEvent->uMessage.sDefaultResponse.u8CommandId,
                    psEvent->uMessage.sDefaultResponse.u8StatusCode);
        break;

    case E_ZCL_CBET_ERROR:
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Error status=%x endpoint=%d\n",
                    psEvent->eZCL_Status,
                    psEvent->u8EndPoint);
        break;

    case E_ZCL_CBET_UNHANDLED_EVENT:
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Unhandled event\n");
        break;

    default:
        DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Unexpected event type=%d\n", psEvent->eEventType);
        break;
    }
}

/**
 * @brief Handles cluster-specific commands
 */
PRIVATE void APP_ZCL_vHandleClusterCustomCommands(tsZCL_CallBackEvent *psEvent)
{
    switch (psEvent->uMessage.sClusterCustomMessage.u16ClusterId) {
    case LIGHTING_CLUSTER_ID_COLOUR_CONTROL: {
        tsCLD_ColourControlCallBackMessage *psMessage =
            (tsCLD_ColourControlCallBackMessage *)
            psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        if (psMessage != NULL &&
            psMessage->u8CommandId ==
                E_CLD_COLOURCONTROL_CMD_MOVE_TO_HUE_AND_SATURATION) {
            tsCLD_ColourControl_MoveToHueAndSaturationCommandPayload *psPayload =
                psMessage->uMessage.psMoveToHueAndSaturationCommandPayload;

            if (psPayload != NULL) {
                char acMessage[24];
                uint8 u8Pos = 7;

                memcpy(acMessage, "RGB,HS,", 7);
                u8Pos += APP_u8AppendDecimal(&acMessage[u8Pos], psPayload->u8Hue);
                acMessage[u8Pos++] = ',';
                u8Pos += APP_u8AppendDecimal(&acMessage[u8Pos], psPayload->u8Saturation);
                acMessage[u8Pos++] = '\n';
                acMessage[u8Pos] = '\0';
                APP_vSendSerialMessage(acMessage);
            }
        }
        break;
    }

    case GENERAL_CLUSTER_ID_BASIC: {
        tsCLD_BasicCallBackMessage *psMessage =
            (tsCLD_BasicCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        if (psMessage->u8CommandId == E_CLD_BASIC_CMD_RESET_TO_FACTORY_DEFAULTS) {
            /* ZCL R6 3.2.2.3.1.1: restore cluster attribute defaults only.
             * IdentifyTime is the only writable attribute enabled in this build.
             * Network state, bindings, groups, reporting and the GP proxy table remain unchanged.
             */
            DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Basic reset to factory defaults received\n");
            sLumiRouter.sIdentifyServerCluster.u16IdentifyTime = 0;
        }
        break;
    }

    case GENERAL_CLUSTER_ID_IDENTIFY: {
        tsCLD_IdentifyCallBackMessage *psMessage =
            (tsCLD_IdentifyCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;

        if (psMessage->u8CommandId == E_CLD_IDENTIFY_CMD_IDENTIFY) {
            /* This module has no physical indicator, so no indication is started or stopped. */
            DBG_vPrintf(TRACE_ZCL,
                        "ZCL Endpoint Callback: Identify time=%d\n",
                        psMessage->uMessage.psIdentifyRequestPayload->u16IdentifyTime);
                        /* Control LED prin Zigbee Identify */
                        if (psMessage->uMessage.psIdentifyRequestPayload->u16IdentifyTime > 0) {
                            APP_vSendSerialMessage("RGB,ON\n");
                        } else {
                            APP_vSendSerialMessage("RGB,OFF\n");
                        }
        }
        else if (psMessage->u8CommandId == E_CLD_IDENTIFY_CMD_IDENTIFY_QUERY) {
            DBG_vPrintf(TRACE_ZCL, "ZCL Endpoint Callback: Identify query\n");
        }
        break;
    }

    case GREENPOWER_CLUSTER_ID: {
        tsGP_GreenPowerCallBackMessage *psMessage =
            (tsGP_GreenPowerCallBackMessage *)psEvent->uMessage.sClusterCustomMessage.pvCustomData;
        APP_vHandleGreenPowerEvent(psMessage);
        break;
    }
    }
}

/**
 * @brief Convert an unsigned 8-bit value to decimal, without sprintf
 */
PRIVATE uint8 APP_u8AppendDecimal(char *pcBuffer, uint8 u8Value)
{
    uint8 u8Pos = 0;
    if (u8Value >= 100) {
        pcBuffer[u8Pos++] = '0' + u8Value / 100;
    }
    if (u8Value >= 10) {
        pcBuffer[u8Pos++] = '0' + (u8Value / 10) % 10;
    }
    pcBuffer[u8Pos++] = '0' + u8Value % 10;
    return u8Pos;
}

/**
 * @brief Handles cluster state updates
 */
PRIVATE void APP_ZCL_vHandleClusterUpdate(tsZCL_CallBackEvent *psEvent)
{
    uint16 u16ClusterId;

    if (psEvent->psClusterInstance == NULL ||
        psEvent->psClusterInstance->psClusterDefinition == NULL) {
        return;
    }

    u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    if (u16ClusterId == GENERAL_CLUSTER_ID_IDENTIFY) {
        DBG_vPrintf(TRACE_ZCL, "Identify time remaining=%d\n",
                    sLumiRouter.sIdentifyServerCluster.u16IdentifyTime);
        return;
    }

    if (u16ClusterId == GENERAL_CLUSTER_ID_ONOFF) {
        static int8 i8LastState = -1;
        int8 i8NewState = sLumiRouter.sOnOffServerCluster.bOnOff ? 1 : 0;

        if (i8NewState != i8LastState) {
            i8LastState = i8NewState;
            APP_vSendSerialMessage(i8NewState ? "RGB,ON\n" : "RGB,OFF\n");
        }
    }

    if (u16ClusterId == GENERAL_CLUSTER_ID_LEVEL_CONTROL) {
        static int16 i16LastLevel = -1;
        uint8 u8Level = sLumiRouter.sLevelControlServerCluster.u8CurrentLevel;

        if (i16LastLevel != (int16)u8Level) {
            char acMessage[20];
            uint8 u8Pos = 10;
            i16LastLevel = (int16)u8Level;

            memcpy(acMessage, "RGB,LEVEL,", 10);
            u8Pos += APP_u8AppendDecimal(&acMessage[u8Pos], u8Level);
            acMessage[u8Pos++] = '\n';
            acMessage[u8Pos] = '\0';
            APP_vSendSerialMessage(acMessage);
        }
    }
}

/**
 * @brief Handles a Configure Reporting record
 */
PRIVATE void APP_ZCL_vHandleConfigureReportingRecord(tsZCL_CallBackEvent *psEvent)
{
    tsZCL_AttributeReportingConfigurationRecord *psRecord = &psEvent->uMessage.sAttributeReportingConfigurationRecord;
    uint16 u16ClusterId = psEvent->psClusterInstance->psClusterDefinition->u16ClusterEnum;

    if (psEvent->eZCL_Status == E_ZCL_SUCCESS) {
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting "
                    "cluster=%04x attribute=%04x type=%d min=%d max=%d\n",
                    u16ClusterId,
                    psRecord->u16AttributeEnum,
                    psRecord->eAttributeDataType,
                    psRecord->u16MinimumReportingInterval,
                    psRecord->u16MaximumReportingInterval);

        APP_vSaveReportableRecord(u16ClusterId, psRecord);
    }
    else if (psEvent->eZCL_Status == E_ZCL_RESTORE_DEFAULT_REPORT_CONFIGURATION) {
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting restore default "
                    "cluster=%04x attribute=%04x\n",
                    u16ClusterId,
                    psRecord->u16AttributeEnum);

        APP_vRestoreDefaultRecord(LUMIROUTER_APPLICATION_ENDPOINT, u16ClusterId, psRecord);
    }
    else {
        /* An empty request may leave the reporting record uninitialized. */
        DBG_vPrintf(TRACE_ZCL,
                    "ZCL Endpoint Callback: Configure reporting failed "
                    "cluster=%04x status=%x\n",
                    u16ClusterId,
                    psEvent->eZCL_Status);
    }
}

/**
 * Experimental Zigbee TTS receiver.
 *
 * Command 0x00: original one-byte test.
 * Command 0x02: short ASCII message, maximum 32 bytes.
 *
 * This stage only logs to UART. No audio playback.
 */
PRIVATE teZCL_Status APP_TTS_eCommandHandler(
    ZPS_tsAfEvent *pZPSEvent,
    tsZCL_EndPointDefinition *psEndPointDefinition,
    tsZCL_ClusterInstance *psClusterInstance)
{
    tsZCL_HeaderParams sHeader;
    uint16 u16HeaderSize;
    uint16 u16PayloadSize;
    uint16 u16Actual[4] = {0, 0, 0, 0};
    uint8 u8Version = 0;
    uint8 u8Level = 0;
    uint8 u8TextLength = 0;
    uint8 au8Text[32] = {0};
    uint8 u8SequenceNumber = 0;
    uint8 i;
    teZCL_Status eStatus;
    char acSerial[50];

    tsZCL_RxPayloadItem asPayload[4] = {
        {1, &u16Actual[0], E_ZCL_UINT8, &u8Version},
        {1, &u16Actual[1], E_ZCL_UINT8, &u8Level},
        {1, &u16Actual[2], E_ZCL_UINT8, &u8TextLength},
        {32, &u16Actual[3], E_ZCL_UINT8, au8Text}
    };

    (void)psEndPointDefinition;

    if (pZPSEvent == NULL ||
        psClusterInstance == NULL ||
        psClusterInstance->psClusterDefinition == NULL ||
        psClusterInstance->psClusterDefinition->u16ClusterEnum !=
            TTS_CLUSTER_ID) {
        return E_ZCL_FAIL;
    }

    u16HeaderSize = u16ZCL_ReadCommandHeader(
        pZPSEvent->uEvent.sApsDataIndEvent.hAPduInst,
        &sHeader);

    u16PayloadSize = PDUM_u16APduInstanceGetPayloadSize(
        pZPSEvent->uEvent.sApsDataIndEvent.hAPduInst);

    if (u16HeaderSize > u16PayloadSize ||
        sHeader.eFrameType !=
            eFRAME_TYPE_COMMAND_IS_SPECIFIC_TO_A_CLUSTER ||
        sHeader.bManufacturerSpecific != TRUE ||
        sHeader.u16ManufacturerCode != ZCL_MANUFACTURER_CODE ||
        sHeader.bDirection != FALSE) {
        return E_ZCL_FAIL;
    }

    /* Keep the previously tested command fully functional. */
    if (sHeader.u8CommandIdentifier == TTS_CMD_FRAGMENT) {
        if (u16PayloadSize - u16HeaderSize != 1) {
            return E_ZCL_FAIL;
        }

        eStatus = eZCL_CustomCommandReceive(
            pZPSEvent, &u8SequenceNumber, asPayload,
            1, E_ZCL_ACCEPT_LESS);

        if (eStatus == E_ZCL_SUCCESS &&
            u16Actual[0] == 1 &&
            u8Version == TTS_PROTOCOL_VERSION) {
            APP_vSendSerialMessage("TTS,TEST,1\n");
        }

        return eStatus;
    }


    /*
     * Fragment command 0x03:
     * version, message_id low/high, level, index, count,
     * length, and 1..64 raw UTF-8 bytes.
     *
     * UART carries hex-encoded bytes; no audio is played.
     */
    if (sHeader.u8CommandIdentifier == TTS_CMD_DATA_FRAGMENT) {
        uint16 au16FragActual[8] = {0};
        uint8 u8FragVersion = 0;
        uint8 u8IdLow = 0;
        uint8 u8IdHigh = 0;
        uint8 u8FragLevel = 0;
        uint8 u8FragIndex = 0;
        uint8 u8FragCount = 0;
        uint8 u8FragLength = 0;
        uint8 au8Fragment[TTS_FRAGMENT_BYTES] = {0};
        uint8 u8J;
        uint16 u16Pos = 0;
        char acFragmentSerial[160];
        const char acHex[] = "0123456789ABCDEF";

        tsZCL_RxPayloadItem asFragmentPayload[8] = {
            {1, &au16FragActual[0], E_ZCL_UINT8, &u8FragVersion},
            {1, &au16FragActual[1], E_ZCL_UINT8, &u8IdLow},
            {1, &au16FragActual[2], E_ZCL_UINT8, &u8IdHigh},
            {1, &au16FragActual[3], E_ZCL_UINT8, &u8FragLevel},
            {1, &au16FragActual[4], E_ZCL_UINT8, &u8FragIndex},
            {1, &au16FragActual[5], E_ZCL_UINT8, &u8FragCount},
            {1, &au16FragActual[6], E_ZCL_UINT8, &u8FragLength},
            {TTS_FRAGMENT_BYTES, &au16FragActual[7],
             E_ZCL_UINT8, au8Fragment}
        };

        if (u16PayloadSize - u16HeaderSize < 8 ||
            u16PayloadSize - u16HeaderSize >
                TTS_FRAGMENT_PAYLOAD_MAX_BYTES) {
            return E_ZCL_FAIL;
        }

        eStatus = eZCL_CustomCommandReceive(
            pZPSEvent,
            &u8SequenceNumber,
            asFragmentPayload,
            8,
            E_ZCL_ACCEPT_LESS);

        if (eStatus != E_ZCL_SUCCESS) {
            return eStatus;
        }

        for (u8J = 0; u8J < 7; u8J++) {
            if (au16FragActual[u8J] != 1) {
                return E_ZCL_FAIL;
            }
        }

        if (u8FragVersion != TTS_PROTOCOL_VERSION ||
            u8FragLevel < TTS_ALERT_INFO ||
            u8FragLevel > TTS_ALERT_URGENT ||
            u8FragCount == 0 ||
            u8FragCount > TTS_MAX_FRAGMENTS ||
            u8FragIndex >= u8FragCount ||
            u8FragLength == 0 ||
            u8FragLength > TTS_FRAGMENT_BYTES ||
            au16FragActual[7] != u8FragLength ||
            u16PayloadSize - u16HeaderSize !=
                TTS_FRAGMENT_HEADER_BYTES + u8FragLength) {
            return E_ZCL_FAIL;
        }

        /* Format: TTS,FRAG,IIII,L,I,C,NN,HEX */
        memcpy(acFragmentSerial, "TTS,FRAG,", 9);
        u16Pos = 9;

        acFragmentSerial[u16Pos++] = acHex[u8IdHigh >> 4];
        acFragmentSerial[u16Pos++] = acHex[u8IdHigh & 15];
        acFragmentSerial[u16Pos++] = acHex[u8IdLow >> 4];
        acFragmentSerial[u16Pos++] = acHex[u8IdLow & 15];
        acFragmentSerial[u16Pos++] = ',';

        acFragmentSerial[u16Pos++] = '0' + u8FragLevel;
        acFragmentSerial[u16Pos++] = ',';
        acFragmentSerial[u16Pos++] = '0' + u8FragIndex;
        acFragmentSerial[u16Pos++] = ',';
        acFragmentSerial[u16Pos++] = '0' + u8FragCount;
        acFragmentSerial[u16Pos++] = ',';

        acFragmentSerial[u16Pos++] = acHex[u8FragLength >> 4];
        acFragmentSerial[u16Pos++] = acHex[u8FragLength & 15];
        acFragmentSerial[u16Pos++] = ',';

        for (u8J = 0; u8J < u8FragLength; u8J++) {
            acFragmentSerial[u16Pos++] =
                acHex[au8Fragment[u8J] >> 4];
            acFragmentSerial[u16Pos++] =
                acHex[au8Fragment[u8J] & 15];
        }

        acFragmentSerial[u16Pos++] = '\n';
        acFragmentSerial[u16Pos] = '\0';

        APP_vSendSerialMessage(acFragmentSerial);
        return E_ZCL_SUCCESS;
    }

    /* New short text command, separate from fragment protocol. */
    if (sHeader.u8CommandIdentifier != 0x02 ||
        u16PayloadSize - u16HeaderSize != 35) {
        return E_ZCL_FAIL;
    }

    eStatus = eZCL_CustomCommandReceive(
        pZPSEvent, &u8SequenceNumber, asPayload,
        4, E_ZCL_ACCEPT_LESS);

    if (eStatus != E_ZCL_SUCCESS ||
        u16Actual[0] != 1 ||
        u16Actual[1] != 1 ||
        u16Actual[2] != 1 ||
        u16Actual[3] != 32 ||
        u8Version != TTS_PROTOCOL_VERSION ||
        u8Level < 1 || u8Level > 3 ||
        u8TextLength == 0 || u8TextLength > 32) {
        return E_ZCL_FAIL;
    }

    /* ASCII only for this first test. No UART control characters. */
    for (i = 0; i < u8TextLength; i++) {
        if (au8Text[i] < 32 || au8Text[i] > 126) {
            return E_ZCL_FAIL;
        }
    }

    /* Reject nonzero padding bytes. */
    for (i = u8TextLength; i < 32; i++) {
        if (au8Text[i] != 0) {
            return E_ZCL_FAIL;
        }
    }

    memcpy(acSerial, "TTS,SHORT,", 10);
    acSerial[10] = '0' + u8Level;
    acSerial[11] = ',';

    for (i = 0; i < u8TextLength; i++) {
        acSerial[12 + i] = (char)au8Text[i];
    }

    acSerial[12 + u8TextLength] = '\n';
    acSerial[13 + u8TextLength] = '\0';

    APP_vSendSerialMessage(acSerial);

    return E_ZCL_SUCCESS;
}

/**
 * @brief Creates cluster instances and registers the application endpoint with ZCL
 */
PRIVATE teZCL_Status APP_ZCL_eRegisterEndPoint(tfpZCL_ZCLCallBackFunction cbCallBack, APP_tsLumiRouter *psDeviceInfo)
{
    teZCL_Status eZCL_Status;

    /* Endpoint configuration */
    psDeviceInfo->sEndPoint.u8EndPointNumber =
        LUMIROUTER_APPLICATION_ENDPOINT;
    psDeviceInfo->sEndPoint.u16ManufacturerCode =
        ZCL_MANUFACTURER_CODE;
    psDeviceInfo->sEndPoint.u16ProfileEnum = HA_PROFILE_ID;
    psDeviceInfo->sEndPoint.bIsManufacturerSpecificProfile = FALSE;
    psDeviceInfo->sEndPoint.u16NumberOfClusters =
        sizeof(APP_tsLumiRouterClusterInstances) /
        sizeof(tsZCL_ClusterInstance);
    psDeviceInfo->sEndPoint.psClusterInstance =
        (tsZCL_ClusterInstance *)&psDeviceInfo->sClusterInstance;
    psDeviceInfo->sEndPoint.bDisableDefaultResponse =
        ZCL_DISABLE_DEFAULT_RESPONSES;
    psDeviceInfo->sEndPoint.pCallBackFunctions = cbCallBack;

    /* Basic */
    eZCL_Status = eCLD_BasicCreateBasic(
        &psDeviceInfo->sClusterInstance.sBasicServer,
        TRUE,
        &sCLD_Basic,
        &psDeviceInfo->sBasicServerCluster,
        &au8BasicClusterAttributeControlBits[0]);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* Identify */
    eZCL_Status = eCLD_IdentifyCreateIdentify(
        &psDeviceInfo->sClusterInstance.sIdentifyServer,
        TRUE,
        &sCLD_Identify,
        &psDeviceInfo->sIdentifyServerCluster,
        &au8IdentifyAttributeControlBits[0],
        &psDeviceInfo->sIdentifyServerCustomDataStructure);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* Device temperature */
    eZCL_Status =
        eCLD_DeviceTemperatureConfigurationCreateDeviceTemperatureConfiguration(
            &psDeviceInfo->sClusterInstance.sDeviceTemperatureConfigurationServer,
            TRUE,
            &sCLD_DeviceTemperatureConfiguration,
            &psDeviceInfo->sDeviceTemperatureConfigurationServerCluster,
            &au8DeviceTempConfigClusterAttributeControlBits[0]);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* RGB: On/Off */
    eZCL_Status = eCLD_OnOffCreateOnOff(
        &psDeviceInfo->sClusterInstance.sOnOffServer,
        TRUE,
        &sCLD_OnOff,
        &psDeviceInfo->sOnOffServerCluster,
        au8RgbOnOffAttrBits,
        &psDeviceInfo->sOnOffServerCustomDataStructure);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* RGB: Brightness */
    eZCL_Status = eCLD_LevelControlCreateLevelControl(
        &psDeviceInfo->sClusterInstance.sLevelControlServer,
        TRUE,
        &sCLD_LevelControl,
        &psDeviceInfo->sLevelControlServerCluster,
        au8RgbLevelAttrBits,
        &psDeviceInfo->sLevelControlServerCustomDataStructure);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* RGB: Colour control */
    eZCL_Status = eCLD_ColourControlCreateColourControl(
        &psDeviceInfo->sClusterInstance.sColourControlServer,
        TRUE,
        &sCLD_ColourControl,
        &psDeviceInfo->sColourControlServerCluster,
        au8RgbColourAttrBits,
        &psDeviceInfo->sColourControlServerCustomDataStructure);

    if (eZCL_Status != E_ZCL_SUCCESS) {
        return eZCL_Status;
    }

    /* Register experimental TTS cluster */
    vZCL_InitializeClusterInstance(
        &psDeviceInfo->sClusterInstance.sTtsServer,
        TRUE,
        &sTtsClusterDefinition,
        &u8TtsClusterData,
        au8TtsAttributeControlBits,
        NULL,
        APP_TTS_eCommandHandler);

    /* Register all clusters */
    return eZCL_Register(&psDeviceInfo->sEndPoint);
}

/**
 * @brief Initialise ZCL device-specific attributes
 */
PRIVATE void APP_ZCL_vDeviceSpecific_Init(void)
{
    memcpy(sLumiRouter.sBasicServerCluster.au8ManufacturerName, BAS_MANUF_NAME_STRING, CLD_BAS_MANUF_NAME_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8ModelIdentifier, BAS_MODEL_ID_STRING, CLD_BAS_MODEL_ID_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8DateCode, BAS_DATE_STRING, CLD_BAS_DATE_SIZE);
    memcpy(sLumiRouter.sBasicServerCluster.au8SWBuildID, BAS_SW_BUILD_STRING, CLD_BAS_SW_BUILD_SIZE);
}
