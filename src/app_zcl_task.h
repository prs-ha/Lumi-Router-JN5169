/**
 * @file  app_zcl_task.h
 * @brief ZCL Interface
 */

#ifndef APP_ZCL_TASK_H
#define APP_ZCL_TASK_H

#include <jendefs.h>

/* SDK JN-SW-4170 */
#include "Basic.h"
#include "Identify.h"
#include "DeviceTemperatureConfiguration.h"
#include "zcl.h"

#include "OnOff.h"
#include "LevelControl.h"
#include "ColourControl.h"

typedef struct {
    tsZCL_ClusterInstance sBasicServer;
    tsZCL_ClusterInstance sIdentifyServer;
    tsZCL_ClusterInstance sDeviceTemperatureConfigurationServer;

    /* RGB Light */
    tsZCL_ClusterInstance sOnOffServer;
    tsZCL_ClusterInstance sLevelControlServer;
    tsZCL_ClusterInstance sColourControlServer;

    /* Experimental Zigbee TTS server */
    tsZCL_ClusterInstance sTtsServer;

} APP_tsLumiRouterClusterInstances __attribute__((aligned(4)));

typedef struct {
    tsZCL_EndPointDefinition sEndPoint;

    /* Cluster instances */
    APP_tsLumiRouterClusterInstances sClusterInstance;

    /* Basic Cluster - Server */
    tsCLD_Basic sBasicServerCluster;

    /* Identify Cluster - Server */
    tsCLD_Identify sIdentifyServerCluster;
    tsCLD_IdentifyCustomDataStructure sIdentifyServerCustomDataStructure;

    /* Device Temperature Configuration Cluster - Server */
    tsCLD_DeviceTemperatureConfiguration sDeviceTemperatureConfigurationServerCluster;

    /* On/Off */
    tsCLD_OnOff sOnOffServerCluster;
    tsCLD_OnOffCustomDataStructure sOnOffServerCustomDataStructure;

    /* Brightness */
    tsCLD_LevelControl sLevelControlServerCluster;
    tsCLD_LevelControlCustomDataStructure sLevelControlServerCustomDataStructure;

    /* Colour */
    tsCLD_ColourControl sColourControlServerCluster;
    tsCLD_ColourControlCustomDataStructure sColourControlServerCustomDataStructure;

} APP_tsLumiRouter;

extern PUBLIC APP_tsLumiRouter sLumiRouter;

PUBLIC void APP_ZCL_vInitialise(void);
PUBLIC void APP_ZCL_vEventHandler(ZPS_tsAfEvent *psStackEvent);
PUBLIC void APP_cbTimerZclTick(void *pvParam);

#endif /* APP_ZCL_TASK_H */
