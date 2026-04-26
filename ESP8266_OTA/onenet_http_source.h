#ifndef ONENET_HTTP_SOURCE_H
#define ONENET_HTTP_SOURCE_H

#include "bootloader_core.h"
#include "onenet_ota.h"

typedef void (*ota_progress_callback_t)(const OtaPackageInfo *info, int progress);

void onenet_http_source_init(const OtaPackageInfo *info);
void onenet_http_source_deinit(void);
void onenet_http_source_set_progress_callback(ota_progress_callback_t callback);

extern const source_if_t onenet_http_source_if;

#endif
