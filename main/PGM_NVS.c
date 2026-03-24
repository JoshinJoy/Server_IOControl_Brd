#include "PGM_NVS.h"

static const char *TAG = "NVS_BOOL";

#define STORAGE_NAMESPACE "storage"
#define TIMER_KEY "timer1"

static SemaphoreHandle_t g_nvs_mutex = NULL;

#define NVS_LOCK()                                      \
    do                                                  \
    {                                                   \
        if (g_nvs_mutex)                                \
            xSemaphoreTake(g_nvs_mutex, portMAX_DELAY); \
    } while (0)
#define NVS_UNLOCK()                     \
    do                                   \
    {                                    \
        if (g_nvs_mutex)                 \
            xSemaphoreGive(g_nvs_mutex); \
    } while (0)

static TimerCfg_t TimerCfg_Default(void)
{
    TimerCfg_t t = {0};
    t.OnHr = 0;
    t.OnMin = 0;
    t.OffHr = 0;
    t.OffMin = 0;
    t.Enable = 0;
    return t;
}

static void NVS_EnsureTimerDefault_Locked(void)
{
    nvs_handle_t h;
    if (nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;

    TimerCfg_t cfg;
    size_t len = sizeof(cfg);

    esp_err_t err = nvs_get_blob(h, TIMER_KEY, &cfg, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg = TimerCfg_Default();
        nvs_set_blob(h, TIMER_KEY, &cfg, sizeof(cfg));
        nvs_commit(h);
    }

    nvs_close(h);
}

void NVS_Init(void)
{
    // Create mutex first
    if (g_nvs_mutex == NULL)
    {
        g_nvs_mutex = xSemaphoreCreateMutex();
    }

    NVS_LOCK();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t handle;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS (%s)", esp_err_to_name(err));
        NVS_UNLOCK();
        return;
    }

    uint8_t value = 0;

    // ---- Timer default (blob) ----
    nvs_close(handle);
    NVS_EnsureTimerDefault_Locked();

    NVS_UNLOCK();
}

bool NVS_GetTimerCfg(TimerCfg_t *out)
{
    if (!out)
        return false;
    NVS_LOCK();
    nvs_handle_t h;
    if (nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    {
        *out = TimerCfg_Default();
        NVS_UNLOCK();
        return false;
    }

    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, TIMER_KEY, out, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(*out))
    {
        *out = TimerCfg_Default();
        NVS_UNLOCK();
        return false;
    }
    NVS_UNLOCK();
    return true;
}

static void clamp_timer(TimerCfg_t *t)
{
    if (t->OnHr > 23)
        t->OnHr = 0;
    if (t->OffHr > 23)
        t->OffHr = 0;
    if (t->OnMin > 59)
        t->OnMin = 0;
    if (t->OffMin > 59)
        t->OffMin = 0;
    t->Enable = (t->Enable != 0) ? 1 : 0;
}

bool NVS_SetTimerCfg(const TimerCfg_t *cfg_in)
{
    if (!cfg_in)
        return false;
    NVS_LOCK();
    TimerCfg_t cfg = *cfg_in;
    clamp_timer(&cfg);

    nvs_handle_t h;
    if (nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    {
        NVS_UNLOCK();
        return false;
    }

    // Optional: avoid flash wear (only write if changed)
    TimerCfg_t old;
    size_t len = sizeof(old);
    esp_err_t err = nvs_get_blob(h, TIMER_KEY, &old, &len);
    if (err == ESP_OK && len == sizeof(old) && memcmp(&old, &cfg, sizeof(cfg)) == 0)
    {
        nvs_close(h);
        NVS_UNLOCK();
        return true; // no change
    }

    err = nvs_set_blob(h, TIMER_KEY, &cfg, sizeof(cfg));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    NVS_UNLOCK();
    return (err == ESP_OK);
}