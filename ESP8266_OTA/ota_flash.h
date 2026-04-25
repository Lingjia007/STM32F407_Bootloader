#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include <stdint.h>

#define BootLoader_addr      0x08000000U
#define BootLoader_Size      0x00020000U

#define Application_addr     0x08020000U
#define Application_Size     0x000E0000U

#define OTA_SPI_FLASH_ADDR   0x400000U
#define OTA_SPI_FLASH_SIZE   0x400000U

#define OTA_META_MAGIC       0x54414F31U

#define OTA_META_STATE_NONE    0U
#define OTA_META_STATE_PENDING 1U

typedef struct
{
    uint32_t magic;
    uint32_t state;
    uint32_t image_size;
    uint8_t  md5[16];
    char     target_version[16];
    char     ota_token[40];
    uint32_t reserved[8];
} OtaBootMeta;

uint32_t OTA_GetDownloadAddr(void);
uint32_t OTA_GetDownloadMaxSize(void);
int OTA_PrepareDownloadArea(uint32_t image_size);
int OTA_WriteDownloadChunk(uint32_t offset, const uint8_t *data, uint32_t len);
void OTA_ReadDownloadData(uint32_t offset, uint8_t *data, uint32_t len);
int OTA_ReadBootMeta(void *meta, uint32_t meta_size);
int OTA_SetPendingImage(uint32_t image_size, const uint8_t md5[16], const char *version, const char *token);
int OTA_ClearPendingImage(void);

#endif
