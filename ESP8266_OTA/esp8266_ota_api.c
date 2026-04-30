#include "esp8266_ota_api.h"
#include "esp8266_ota_config.h"

static onenet_ota_ctx_t g_ota_ctx;

void esp8266_ota_init(void)
{
    onenet_ota_ctx_init(&g_ota_ctx, &g_esp8266_wifi.base, &g_rtc.base);
}

void esp8266_ota_set_target_internal_flash(void)
{
    onenet_ota_set_target(&g_ota_ctx, ONENET_OTA_TARGET_INTERNAL_FLASH);
}

void esp8266_ota_set_target_sd_card(void)
{
    onenet_ota_set_target(&g_ota_ctx, ONENET_OTA_TARGET_SD_CARD_FATFS);
}

void esp8266_ota_set_target_spi_flash(void)
{
    onenet_ota_set_target(&g_ota_ctx, ONENET_OTA_TARGET_SPI_FLASH_LFS);
}

int esp8266_ota_download(void)
{
    onenet_ota_process_upgrade(&g_ota_ctx);
    return 1;
}

int esp8266_ota_sync_time(void)
{
    return onenet_ota_sync_time(&g_ota_ctx);
}
