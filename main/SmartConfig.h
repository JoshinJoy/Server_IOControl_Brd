#ifndef SMARTCONFIG_H
#define SMARTCONFIG_H

#include "main.h"

#define GUI_MSG_MAX 128

typedef struct
{
    char text[GUI_MSG_MAX];
} gui_msg_t;

extern QueueHandle_t g_guiQueue;    

void SmartConfigWifi_Init();
bool Get_IsWifiConnected();
bool Get_IsSmartConfigActive();
void RunSamrtConfig();

#endif // !SMARTCONFIG_H