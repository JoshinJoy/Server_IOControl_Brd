#include "main.h"

typedef struct __attribute__((packed))
{
    uint8_t OnMin;
    uint8_t OnHr;
    uint8_t OffMin;
    uint8_t OffHr;
    uint8_t Enable; // store bool as 0/1 for stable size
} TimerCfg_t;

void NVS_Init();