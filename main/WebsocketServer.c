#include "WebsocketServer.h"
#include "SmartConfig.h"

#define MAX_WS_CLIENTS 5

static int ws_clients[MAX_WS_CLIENTS];
static int ws_client_count = 0;
static httpd_handle_t g_server = NULL;
static SemaphoreHandle_t WebSocketTx_Mutex;

struct async_resp_arg
{
    httpd_handle_t hd;
    int fd;
};

static esp_err_t ws_send_text_async(int fd, const char *msg)
{
    if (!g_server || !msg)
        return ESP_FAIL;

    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = (uint8_t *)msg;
    pkt.len = strlen(msg);

    return httpd_ws_send_frame_async(g_server, fd, &pkt);
}

static void ws_reply_json(int fd, const char *type, const char *msg)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "type", type);
    if (msg)
        cJSON_AddStringToObject(r, "msg", msg);

    char *s = cJSON_PrintUnformatted(r);
    if (s)
    {
        ws_send_text_async(fd, s);
        cJSON_free(s);
    }
    cJSON_Delete(r);
}

static void ws_process_json_cmd(int fd, const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root)
    {
        cJSON_Delete(root);
        ws_reply_json(fd, "error", "bad_json");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd))
    {
        cJSON_Delete(root);
        ws_reply_json(fd, "error", "missing_cmd");
        return;
    }

    if (strcmp(cmd->valuestring, "ping") == 0)
    {
        ws_reply_json(fd, "pong", "ok");
    }
    else
    {
        ws_reply_json(fd, "error", "unknown_cmd");
    }

    cJSON_Delete(root);
}

static esp_err_t echo_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        // ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        int fd = httpd_req_to_sockfd(req);

        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            if (ws_clients[i] == -1)
            {
                ws_clients[i] = fd;
                ws_client_count++;
                break;
            }
        }
        return ESP_OK;
    }
    // 2) Receive WS frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        // Could also remove client here if needed
        return ret;
    }

    if (ws_pkt.len == 0)
    {
        return ESP_OK;
    }

    uint8_t *buf = (uint8_t *)calloc(1, ws_pkt.len + 1);
    if (!buf)
        return ESP_ERR_NO_MEM;

    ws_pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK)
    {
        free(buf);
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);

    // Only handle TEXT as JSON in this example
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.payload)
    {
        const char *json_str = (const char *)ws_pkt.payload;
        ws_process_json_cmd(fd, json_str);

        // IMPORTANT: no echo send here
        free(buf);
        return ESP_OK;
    }

    // If you want to ignore binary/ping/pong etc:
    free(buf);
    return ESP_OK;
}

static const httpd_uri_t ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = echo_handler,
    .user_ctx = NULL,
    .is_websocket = true};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.stack_size = 1024 * 10;

    // Start the httpd server
    // ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Registering the ws handler
        // ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ws);
        return server;
    }

    // ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void WebsocketServer_Task(void *pvParameter)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
        ws_clients[i] = -1;
    ws_client_count = 0;

    while (1)
    {
        if (Get_IsWifiConnected())
        {
            g_server = start_webserver();
            vTaskDelete(NULL); // Delete the task after starting the server
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void WebsocketServer_Init(void)
{
    WebSocketTx_Mutex = xSemaphoreCreateMutex();
    xTaskCreate(WebsocketServer_Task, "WebsocketServer_Task", 1024 * 10, NULL, 5, NULL);
}
