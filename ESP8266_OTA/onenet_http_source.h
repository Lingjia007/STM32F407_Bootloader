#ifndef ONENET_HTTP_SOURCE_H
#define ONENET_HTTP_SOURCE_H

#include "platform_storage.h"
#include "onenet_ota.h"

typedef void (*ota_progress_callback_t)(const OtaPackageInfo *info, int progress);

void onenet_http_source_init(const OtaPackageInfo *info);
void onenet_http_source_deinit(void);
void onenet_http_source_set_progress_callback(ota_progress_callback_t callback);

extern platform_storage_base_t g_onenet_http_source;

#endif
