#include "main.h"

#include "PGM_NVS.h"
#include "PGM_Mdns.h"
#include "PGM_SNTP.h"
#include "SmartConfig.h"
#include "WebsocketServer.h"

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    NVS_Init();
    SmartConfigWifi_Init();
    WebsocketServer_Init();
    initialise_mdns();
    PGM_SNTP_Init();
}
