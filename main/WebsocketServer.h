#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H

#include "main.h"

extern QueueHandle_t WebSocketTx_queue;

void WebsocketServer_Init(void);
void ws_broadcast_text(const char *msg);

#endif // WEBSOCKETSERVER_H