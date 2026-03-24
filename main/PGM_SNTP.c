#include "PGM_SNTP.h"
#include "SmartConfig.h"

const char *TAG = "PGM_SNTP";

static SemaphoreHandle_t SNTP_Mutex;
bool IsTimeCorrected = false;

static void PGM_SNTP_UpdateRTC_FromSystemTime(void)
{
    time_t now = time(NULL);

    // Basic validity check (avoid writing garbage time)
    // 1700000000 ~ Nov 2023. Pick any threshold you like.
    if (now < 1700000000)
    {
        ESP_LOGW(TAG, "System time not valid yet, skip RTC update");
        return;
    }
}

static void ntp_sync_task(void *pvParameter)
{
    // Wait WiFi
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while (!Get_IsWifiConnected())
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Configure + start SNTP
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = false;
    cfg.server_from_dhcp = true;
    cfg.renew_servers_after_new_IP = true;
    cfg.index_of_first_server = 1;

    esp_netif_sntp_init(&cfg);
    esp_netif_sntp_start();
    ESP_LOGI(TAG, "SNTP started, waiting for sync...");

    // Wait until sync success
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_ERR_TIMEOUT)
    {
        ESP_LOGI(TAG, "SNTP timeout, retrying...");
    }

    ESP_LOGI(TAG, "SNTP sync successful!");

    // Set timezone once
    setenv("TZ", "IST-5:30", 1);
    tzset();

    // Mark corrected
    xSemaphoreTake(SNTP_Mutex, portMAX_DELAY);
    IsTimeCorrected = true;
    xSemaphoreGive(SNTP_Mutex);

    // ✅ First-time RTC update (power-up)
    PGM_SNTP_UpdateRTC_FromSystemTime();

    // ✅ Then update RTC every 24 hours
    const TickType_t one_day = pdMS_TO_TICKS(24UL * 60UL * 60UL * 1000UL);

    while (1)
    {
        vTaskDelay(one_day);

        // Optional: ensure WiFi is connected before updating
        if (!Get_IsWifiConnected())
        {
            ESP_LOGW(TAG, "WiFi not connected, skipping 24h RTC update");
            continue;
        }

        // Use system time (SNTP keeps it corrected)
        PGM_SNTP_UpdateRTC_FromSystemTime();
    }
}

void PGM_SNTP_Init(void)
{
    SNTP_Mutex = xSemaphoreCreateMutex();
    xTaskCreate(ntp_sync_task, "ntp_sync_task", 4096, NULL, 5, NULL);
}

bool PGM_SNTP_IsTimeCorrected(void)
{
    bool time_corrected = false;
    if (xSemaphoreTake(SNTP_Mutex, portMAX_DELAY) == pdTRUE)
    {
        time_corrected = IsTimeCorrected;
        xSemaphoreGive(SNTP_Mutex);
    }
    return time_corrected;
}

struct tm PGM_SNTP_GetLocalTime(void)
{
    struct tm local_time;
    if (xSemaphoreTake(SNTP_Mutex, portMAX_DELAY) == pdTRUE)
    {
        time_t now = time(NULL);
        localtime_r(&now, &local_time);
        xSemaphoreGive(SNTP_Mutex);
    }
    return local_time;
}