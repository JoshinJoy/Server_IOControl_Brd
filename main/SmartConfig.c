#include "SmartConfig.h"

QueueHandle_t g_guiQueue;

static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static SemaphoreHandle_t WifiConnect_Mutex;
static bool smartconfig_active_Flg = false; // Flag to track SmartConfig state

static TaskHandle_t SmartConfigTaskHandle = NULL;

void SendLog(const char *msg);

bool Get_IsSmartConfigActive()
{
    if (xSemaphoreTake(WifiConnect_Mutex, portMAX_DELAY) == pdTRUE)
    {
        bool is_active = smartconfig_active_Flg;
        xSemaphoreGive(WifiConnect_Mutex);
        return is_active;
    }
    return false;
}

void Set_SmartConfigActive(bool active)
{
    if (xSemaphoreTake(WifiConnect_Mutex, portMAX_DELAY) == pdTRUE)
    {
        smartconfig_active_Flg = active;
        xSemaphoreGive(WifiConnect_Mutex);
    }
}

bool Get_IsWifiConnected()
{
    if (xSemaphoreTake(WifiConnect_Mutex, portMAX_DELAY) == pdTRUE)
    {
        EventBits_t uxBits = xEventGroupGetBits(s_wifi_event_group);
        xSemaphoreGive(WifiConnect_Mutex);
        return (uxBits & CONNECTED_BIT) != 0;
    }
    return false;
}

// Event handler for Wi-Fi and SmartConfig events
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (Get_IsSmartConfigActive() == false)
        {
            esp_wifi_connect();
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        SendLog("WiFi Connected, IP acquired.");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        SendLog("Got SSID and password");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

        wifi_config_t wifi_config;
        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

        // ESP_LOGI(TAG, "SSID: %s", wifi_config.sta.ssid);
        // ESP_LOGI(TAG, "PASSWORD: %s", wifi_config.sta.password);

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}


void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Combined SmartConfig and button handling task
static void SmartConfig_task(void *arg)
{
    EventBits_t uxBits;

    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!Get_IsSmartConfigActive()) // Button held for 5 seconds
        {
            SendLog("Starting SmartConfig");
            Set_SmartConfigActive(true); // Set SmartConfig active flag

            if (esp_wifi_get_mode(NULL) == ESP_OK)
            {
                SendLog("Disconnecting Wi-Fi connection");
                ESP_ERROR_CHECK(esp_wifi_disconnect());
                ESP_ERROR_CHECK(esp_wifi_stop());
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            // Start SmartConfig
            ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
            esp_esptouch_set_timeout(60);
            smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

            SendLog("SmartConfig started");

            // Wait for SmartConfig to complete
            while (1)
            {
                xSemaphoreTake(WifiConnect_Mutex, portMAX_DELAY);
                uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
                xSemaphoreGive(WifiConnect_Mutex);
                if (uxBits & CONNECTED_BIT)
                {
                    SendLog("WiFi Connected to AP");
                }
                if (uxBits & ESPTOUCH_DONE_BIT)
                {
                    SendLog("SmartConfig over");

                    esp_smartconfig_stop();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    ESP_ERROR_CHECK(esp_wifi_disconnect());
                    ESP_ERROR_CHECK(esp_wifi_stop());
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    esp_restart();
                    Set_SmartConfigActive(false); // Reset SmartConfig active flag
                    break;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Polling interval
    }
}

void SmartConfigWifi_Init()
{
    WifiConnect_Mutex = xSemaphoreCreateMutex();
    g_guiQueue = xQueueCreate(10, sizeof(gui_msg_t)); // 10 messages deep

    // Initialize Wi-Fi
    initialise_wifi();

    // Start SmartConfig task
    xTaskCreate(SmartConfig_task, "SmartConfig_task", 1024 * 4, NULL, 5, &SmartConfigTaskHandle);
}

void RunSamrtConfig()
{
    if (SmartConfigTaskHandle)
    {
        xTaskNotifyGive(SmartConfigTaskHandle);
    }
}

void SendLog(const char *msg)
{
    if (g_guiQueue == NULL || msg == NULL)
        return;

    gui_msg_t log_msg;

    snprintf(log_msg.text, GUI_MSG_MAX, "%s\n", msg);

    xQueueSend(g_guiQueue, &log_msg, 0); // non-blocking for logs
}