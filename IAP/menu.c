/**
  ******************************************************************************
  * @file    IAP/IAP_Main/Src/menu.c
  * @author  MCD Application Team

  * @brief   This file provides the software which contains the main menu routine.
  *          The main menu gives the options of:
  *             - downloading a new binary file,
  *             - uploading internal flash memory,
  *             - executing the binary file already loaded
  *             - configuring the write protection of the Flash sectors where the
  *               user loads his binary file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/** @addtogroup STM32F4xx_IAP_Main
 * @{
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "common.h"
#include "flash_if.h"
#include "menu.h"
#include "ymodem.h"
#include "fatfs.h"
#include "lfs_spi_flash_adapter.h"
#include "bootloader_core.h"
#include "lfs.h"
#include "stdio.h"
#include "string.h"
#include "ctype.h"
#include "aes_decrypt.h"
#include "hpatch_upgrade.h"
#include "usart.h"
#include "esp8266_driver.h"
#include "esp8266_ota_api.h"
#include "onenet_ota.h"
#include "esp8266_ota_config.h"
#include "rtc.h"
#include "esp8266_mqtt.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define MAX_FILES 20
#define MAX_FILENAME_LEN 128

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
pFunction JumpToApplication;
uint32_t JumpAddress;
uint32_t FlashProtection = 0;
uint8_t aFileName[FILE_NAME_LENGTH];

static char file_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t file_count = 0;

static char hdiff_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t hdiff_count = 0;

/* Private function prototypes -----------------------------------------------*/
void SerialDownload(void);
void SerialUpload(void);
void SDCardDownload(void);
void SPIFlashDownload(void);
static void DecryptAESFile(void);
static void DecryptAndDownloadMenu(void);
static void BuildSelectionPrompt(char *msg, size_t msg_size, uint8_t file_count, const char *prefix);
static void scan_sd_card_hdiff_files(void);
static void HPatchUpgradeMenu(void);
static void UART_Passthrough(void);
static void ESP8266_TestMenu(void);
static void MQTT_TestMenu(void);

/* Private functions ---------------------------------------------------------*/

static uint8_t check_file_extension(const char *filename)
{
  const char *ext = NULL;
  size_t len = strlen(filename);

  if (len < 4)
  {
    return 0;
  }

  ext = filename + len - 4;
  if (strcmp(ext, ".bin") == 0 || strcmp(ext, ".BIN") == 0)
  {
    return 1;
  }

  if (len >= 6)
  {
    ext = filename + len - 6;
    if (strcmp(ext, ".hdiff") == 0 || strcmp(ext, ".HDIFF") == 0)
    {
      return 2;
    }
  }

  if (len >= 8)
  {
    ext = filename + len - 8;
    if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
    {
      return 1;
    }
  }

  return 0;
}

static void BuildSelectionPrompt(char *msg, size_t msg_size, uint8_t file_count, const char *prefix)
{
  snprintf(msg, msg_size, "%s (1-%d) or 'a' to abort: ", prefix, file_count);
}

static void scan_sd_card_files(void)
{
  DIR dir;
  FILINFO fno;
  FRESULT res;

  file_count = 0;

  res = f_opendir(&dir, "0:/");
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SD card directory!\r\n");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
    {
      break;
    }

    if (!(fno.fattrib & AM_DIR))
    {
      if (check_file_extension(fno.fname))
      {
        strncpy(file_list[file_count], fno.fname, MAX_FILENAME_LEN - 1);
        file_list[file_count][MAX_FILENAME_LEN - 1] = '\0';
        file_count++;
      }
    }
  }

  f_closedir(&dir);
}

static void scan_sd_card_files_filter(uint8_t filter_type)
{
  DIR dir;
  FILINFO fno;
  FRESULT res;

  file_count = 0;

  res = f_opendir(&dir, "0:/");
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SD card directory!\r\n");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
    {
      break;
    }

    if (!(fno.fattrib & AM_DIR))
    {
      uint8_t ext_type = check_file_extension(fno.fname);
      if (ext_type == filter_type || (filter_type == 0 && ext_type))
      {
        strncpy(file_list[file_count], fno.fname, MAX_FILENAME_LEN - 1);
        file_list[file_count][MAX_FILENAME_LEN - 1] = '\0';
        file_count++;
      }
    }
  }

  f_closedir(&dir);
}

static void scan_lfs_files(lfs_t *lfs)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;

  file_count = 0;

  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SPI Flash directory!\r\n");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
    {
      break;
    }

    if (info.type == LFS_TYPE_REG)
    {
      if (check_file_extension(info.name))
      {
        strncpy(file_list[file_count], info.name, MAX_FILENAME_LEN - 1);
        file_list[file_count][MAX_FILENAME_LEN - 1] = '\0';
        file_count++;
      }
    }
  }

  lfs_dir_close(lfs, &dir);
}

static void scan_lfs_files_filter(lfs_t *lfs, uint8_t filter_type)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;

  file_count = 0;

  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SPI Flash directory!\r\n");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
    {
      break;
    }

    if (info.type == LFS_TYPE_REG)
    {
      uint8_t ext_type = check_file_extension(info.name);
      if (ext_type == filter_type || (filter_type == 0 && ext_type))
      {
        strncpy(file_list[file_count], info.name, MAX_FILENAME_LEN - 1);
        file_list[file_count][MAX_FILENAME_LEN - 1] = '\0';
        file_count++;
      }
    }
  }

  lfs_dir_close(lfs, &dir);
}

static void scan_lfs_hdiff_files(lfs_t *lfs)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;

  hdiff_count = 0;

  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SPI Flash directory!\r\n");
    return;
  }

  while (hdiff_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
    {
      break;
    }

    if (info.type == LFS_TYPE_REG)
    {
      size_t len = strlen(info.name);
      if (len >= 6)
      {
        const char *ext = info.name + len - 6;
        if (strcmp(ext, ".hdiff") == 0 || strcmp(ext, ".HDIFF") == 0)
        {
          strncpy(hdiff_list[hdiff_count], info.name, MAX_FILENAME_LEN - 1);
          hdiff_list[hdiff_count][MAX_FILENAME_LEN - 1] = '\0';
          hdiff_count++;
        }
      }
    }
  }

  lfs_dir_close(lfs, &dir);
}

static void ImageDownloadMenu(void)
{
  uint8_t key = 0;

  while (1)
  {
    Serial_PutString((uint8_t *)"\r\n=================== Image Download Menu =================\r\n\n");
    Serial_PutString((uint8_t *)"  Download via Serial (Ymodem) ------------------------ 1\r\n\n");
    Serial_PutString((uint8_t *)"  Download from SD card (FATFS) ----------------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  Download from SPI Flash (LittleFS) ------------------ 3\r\n\n");
    Serial_PutString((uint8_t *)"  Return to Main Menu --------------------------------- 0\r\n\n");
    Serial_PutString((uint8_t *)"==========================================================\r\n\n");

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '0':
      Serial_PutString((uint8_t *)"\r\nReturn to Main Menu...\r\n");
      return;
    case '1':
      SerialDownload();
      break;
    case '2':
      SDCardDownload();
      break;
    case '3':
      SPIFlashDownload();
      break;
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 0, 1, 2 or 3\r");
      break;
    }
  }
}

static void ShowStoredImages(lfs_t *lfs)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;
  uint8_t count = 0;
  char msg[128];

  Serial_PutString((uint8_t *)"\r\nStored images in SPI Flash:\r\n");
  Serial_PutString((uint8_t *)"========================================\r\n");

  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: Cannot open directory!\r\n");
    return;
  }

  while (1)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
    {
      break;
    }

    if (info.type == LFS_TYPE_REG)
    {
      count++;
      snprintf(msg, sizeof(msg), "  [%d] %s  Size: %u bytes\r\n", count, info.name, (unsigned int)info.size);
      Serial_PutString((uint8_t *)msg);
    }
  }

  lfs_dir_close(lfs, &dir);

  if (count == 0)
  {
    Serial_PutString((uint8_t *)"  No images stored.\r\n");
  }

  Serial_PutString((uint8_t *)"========================================\r\n");
}

static void DeleteStoredImage(lfs_t *lfs)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  int res;

  scan_lfs_files(lfs);

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"\r\nNo images found to delete!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"\r\nSelect image to delete:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nEnter selection");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nDeleting: %s\r\n", file_list[selected - 1]);
  Serial_PutString((uint8_t *)msg);

  res = lfs_remove(lfs, file_list[selected - 1]);
  if (res == LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Image deleted successfully!\r\n");
  }
  else
  {
    Serial_PutString((uint8_t *)"Error: Failed to delete image!\r\n");
  }
}

static void DeleteEntireFS(lfs_t *lfs)
{
  uint8_t key = 0;
  int res;

  Serial_PutString((uint8_t *)"\r\nWARNING: This will delete ALL files in SPI Flash!\r\n");
  Serial_PutString((uint8_t *)"Press 'y' to confirm, any other key to abort: ");

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
  HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

  if (key != 'y' && key != 'Y')
  {
    Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"\r\nFormatting SPI Flash file system...\r\n");

  res = lfs_unmount(lfs);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Warning: Unmount failed, continuing...\r\n");
  }

  res = lfs_format(lfs, &lfs_spi_flash_cfg);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: Format failed!\r\n");
    return;
  }

  res = lfs_mount(lfs, &lfs_spi_flash_cfg);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: Remount failed!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"File system formatted successfully!\r\n");
}

static void StoreFromTFCard(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  int res;
  bootloader_err_t err;
  lfs_t lfs;

  Serial_PutString((uint8_t *)"\r\nInitializing TF card...\r\n");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"Error: SD card mount failed! Error code: ");
    Int2Str((uint8_t *)msg, res);
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Initializing SPI Flash...\r\n");

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    Serial_PutString((uint8_t *)"Error: SPI Flash initialization failed!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Mounting LittleFS...\r\n");

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: LittleFS mount failed!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Scanning TF card for bin, aes and hdiff files...\r\n\r\n");

  scan_sd_card_files();

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No bin, aes or hdiff files found on TF card!\r\n");
    lfs_spi_flash_unmount(&lfs);
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Found files:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nSelect file to store");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      lfs_spi_flash_unmount(&lfs);
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected - 1]);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStoring image to SPI Flash...\r\n");

  bootloader_ctx.config.storage.fatfs = &SDFatFS;
  bootloader_ctx.config.storage.lfs = &lfs;
  strncpy(bootloader_ctx.config.storage.fatfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.fatfs_path) - 1);
  bootloader_ctx.config.storage.fatfs_path[sizeof(bootloader_ctx.config.storage.fatfs_path) - 1] = '\0';
  strncpy(bootloader_ctx.config.storage.lfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.lfs_path) - 1);
  bootloader_ctx.config.storage.lfs_path[sizeof(bootloader_ctx.config.storage.lfs_path) - 1] = '\0';

  err = bootloader_download(&fatfs_source_if, &lfs_target_if, NULL);

  if (err == BOOTLOADER_OK)
  {
    Serial_PutString((uint8_t *)"\r\nImage stored successfully!\r\n");
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nStore failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-err));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
  }

  lfs_spi_flash_unmount(&lfs);
  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void StoreFromInternalFlash(void)
{
  uint8_t key = 0;
  char filename[64];
  char msg[256];
  char size_str[16];
  uint8_t idx = 0;
  uint32_t size = 0;
  uint32_t flash_addr;
  uint32_t flash_size;
  int res;
  lfs_t lfs;
  lfs_file_t file;
  uint8_t buffer[256];
  uint32_t bytes_written = 0;
  uint32_t total_written = 0;

  Serial_PutString((uint8_t *)"\r\nInitializing SPI Flash...\r\n");

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    Serial_PutString((uint8_t *)"Error: SPI Flash initialization failed!\r\n");
    return;
  }

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: LittleFS mount failed!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"\r\nEnter filename (with .bin or .bin.aes extension): ");

  idx = 0;
  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == '\r' || key == '\n')
    {
      filename[idx] = '\0';
      break;
    }
    else if (key == 0x08 || key == 0x7F)
    {
      if (idx > 0)
      {
        idx--;
        Serial_PutString((uint8_t *)"\b \b");
      }
    }
    else if (idx < sizeof(filename) - 1 && key >= 0x20 && key <= 0x7E)
    {
      filename[idx++] = key;
      HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
    }
  }

  if (idx == 0)
  {
    Serial_PutString((uint8_t *)"\r\nError: Empty filename!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  if (!check_file_extension(filename))
  {
    Serial_PutString((uint8_t *)"\r\nError: Invalid file extension! Must be .bin or .bin.aes\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  Serial_PutString((uint8_t *)"\r\nEnter size in bytes (or 'm' for max): ");

  idx = 0;
  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == '\r' || key == '\n')
    {
      size_str[idx] = '\0';
      break;
    }
    else if (key == 0x08 || key == 0x7F)
    {
      if (idx > 0)
      {
        idx--;
        Serial_PutString((uint8_t *)"\b \b");
      }
    }
    else if (idx < sizeof(size_str) - 1)
    {
      if ((key >= '0' && key <= '9') || (idx == 0 && (key == 'm' || key == 'M')))
      {
        size_str[idx++] = key;
        HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
      }
    }
  }

  if (idx == 0)
  {
    Serial_PutString((uint8_t *)"\r\nError: Empty size!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  if (size_str[0] == 'm' || size_str[0] == 'M')
  {
    size = USER_FLASH_SIZE;
  }
  else
  {
    size = 0;
    for (idx = 0; size_str[idx] != '\0'; idx++)
    {
      if (size_str[idx] >= '0' && size_str[idx] <= '9')
      {
        size = size * 10 + (size_str[idx] - '0');
      }
    }
  }

  if (size == 0 || size > USER_FLASH_SIZE)
  {
    Serial_PutString((uint8_t *)"\r\nError: Invalid size!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  snprintf(msg, sizeof(msg), "\r\nFilename: %s, Size: %u bytes\r\n", filename, (unsigned int)size);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"Confirm? (y/n): ");

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
  HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

  if (key != 'y' && key != 'Y')
  {
    Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  Serial_PutString((uint8_t *)"\r\nStoring from Internal Flash to SPI Flash...\r\n");

  res = lfs_file_open(&lfs, &file, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: Cannot create file!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  flash_addr = APPLICATION_ADDRESS;
  flash_size = size;

  while (total_written < flash_size)
  {
    uint32_t to_write = sizeof(buffer);
    if (flash_size - total_written < to_write)
    {
      to_write = flash_size - total_written;
    }

    memcpy(buffer, (uint8_t *)(flash_addr + total_written), to_write);

    bytes_written = lfs_file_write(&lfs, &file, buffer, to_write);
    if (bytes_written != to_write)
    {
      Serial_PutString((uint8_t *)"Error: Write failed!\r\n");
      lfs_file_close(&lfs, &file);
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    total_written += to_write;

    if ((total_written % 4096) == 0 || total_written == flash_size)
    {
      snprintf(msg, sizeof(msg), "Progress: %u / %u bytes\r", (unsigned int)total_written, (unsigned int)flash_size);
      Serial_PutString((uint8_t *)msg);
    }
  }

  lfs_file_close(&lfs, &file);
  lfs_spi_flash_unmount(&lfs);

  Serial_PutString((uint8_t *)"\r\nImage stored successfully!\r\n");
}

static void StoreImageMenu(void)
{
  uint8_t key = 0;
  lfs_t lfs;
  int res;

  while (1)
  {
    Serial_PutString((uint8_t *)"\r\n============== Store Image Menu ==============\r\n\n");
    Serial_PutString((uint8_t *)"  Store image from TF card ------------ 1\r\n\n");
    Serial_PutString((uint8_t *)"  Store image from Flash -------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  Show stored images ------------------ 3\r\n\n");
    Serial_PutString((uint8_t *)"  Delete stored image ----------------- 4\r\n\n");
    Serial_PutString((uint8_t *)"  Delete entire file system ----------- 5\r\n\n");
    Serial_PutString((uint8_t *)"  Return to main menu ----------------- 0\r\n\n");
    Serial_PutString((uint8_t *)"==============================================\r\n\n");

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '0':
      Serial_PutString((uint8_t *)"\r\nReturn to Main Menu...\r\n");
      return;
    case '1':
      StoreFromTFCard();
      break;
    case '2':
      StoreFromInternalFlash();
      break;
    case '3':
      res = lfs_spi_flash_init();
      if (res == 0)
      {
        res = lfs_spi_flash_mount(&lfs);
        if (res == LFS_ERR_OK)
        {
          ShowStoredImages(&lfs);
          lfs_spi_flash_unmount(&lfs);
        }
        else
        {
          Serial_PutString((uint8_t *)"\r\nError: Cannot mount SPI Flash!\r\n");
        }
      }
      else
      {
        Serial_PutString((uint8_t *)"\r\nError: SPI Flash init failed!\r\n");
      }
      break;
    case '4':
      res = lfs_spi_flash_init();
      if (res == 0)
      {
        res = lfs_spi_flash_mount(&lfs);
        if (res == LFS_ERR_OK)
        {
          DeleteStoredImage(&lfs);
          lfs_spi_flash_unmount(&lfs);
        }
        else
        {
          Serial_PutString((uint8_t *)"\r\nError: Cannot mount SPI Flash!\r\n");
        }
      }
      else
      {
        Serial_PutString((uint8_t *)"\r\nError: SPI Flash init failed!\r\n");
      }
      break;
    case '5':
      res = lfs_spi_flash_init();
      if (res == 0)
      {
        res = lfs_spi_flash_mount(&lfs);
        if (res == LFS_ERR_OK)
        {
          DeleteEntireFS(&lfs);
          lfs_spi_flash_unmount(&lfs);
        }
        else
        {
          Serial_PutString((uint8_t *)"\r\nError: Cannot mount SPI Flash!\r\n");
        }
      }
      else
      {
        Serial_PutString((uint8_t *)"\r\nError: SPI Flash init failed!\r\n");
      }
      break;
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 0-5\r");
      break;
    }
  }
}

void SDCardDownload(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  FRESULT res;
  bootloader_err_t err;

  Serial_PutString((uint8_t *)"\r\nInitializing TF card...\r\n");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"Error: SD card mount failed! Error code: ");
    Int2Str((uint8_t *)msg, res);
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Scanning for bin and aes files...\r\n\r\n");

  scan_sd_card_files();

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No bin or aes files found on SD card!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Found bin and aes files:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nPlease select a file");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
      {
        break;
      }
    }

    if (file_count >= 10)
    {
      if (key >= '0' && key <= '9')
      {
        selected = (selected * 10) + (key - '0');
        if (selected >= 1 && selected <= file_count)
        {
          break;
        }
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected - 1]);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStarting firmware update...\r\n");

  bootloader_ctx.config.storage.fatfs = &SDFatFS;
  strncpy(bootloader_ctx.config.storage.fatfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.fatfs_path) - 1);
  bootloader_ctx.config.storage.fatfs_path[sizeof(bootloader_ctx.config.storage.fatfs_path) - 1] = '\0';
  bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;

  err = bootloader_download(&fatfs_source_if,
                            &internal_flash_target_if,
                            NULL);

  if (err == BOOTLOADER_OK)
  {
    Serial_PutString((uint8_t *)"\r\nFirmware update completed successfully!\r\n");
    Serial_PutString((uint8_t *)"You can now execute the application.\r\n");
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nFirmware update failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-err));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

void SPIFlashDownload(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  int res;
  bootloader_err_t err;
  lfs_t lfs;

  Serial_PutString((uint8_t *)"\r\nInitializing SPI Flash...\r\n");

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    Serial_PutString((uint8_t *)"Error: SPI Flash initialization failed!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Mounting LittleFS...\r\n");

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: LittleFS mount failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-res));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Scanning for bin and aes files...\r\n\r\n");

  scan_lfs_files(&lfs);

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No bin or aes files found on SPI Flash!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  Serial_PutString((uint8_t *)"Found bin and aes files:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nPlease select a file");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
      {
        break;
      }
    }

    if (file_count >= 10)
    {
      if (key >= '0' && key <= '9')
      {
        selected = (selected * 10) + (key - '0');
        if (selected >= 1 && selected <= file_count)
        {
          break;
        }
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected - 1]);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStarting firmware update...\r\n");

  bootloader_ctx.config.storage.lfs = &lfs;
  strncpy(bootloader_ctx.config.storage.lfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.lfs_path) - 1);
  bootloader_ctx.config.storage.lfs_path[sizeof(bootloader_ctx.config.storage.lfs_path) - 1] = '\0';
  bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;

  err = bootloader_download(&lfs_source_if,
                            &internal_flash_target_if,
                            NULL);

  if (err == BOOTLOADER_OK)
  {
    Serial_PutString((uint8_t *)"\r\nFirmware update completed successfully!\r\n");
    Serial_PutString((uint8_t *)"You can now execute the application.\r\n");
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nFirmware update failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-err));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
  }

  lfs_spi_flash_unmount(&lfs);
}

/**
 * @brief  Download a file via serial port
 * @param  None
 * @retval None
 */
void SerialDownload(void)
{
  uint8_t number[11] = {0};
  uint32_t size = 0;
  COM_StatusTypeDef result;

  Serial_PutString((uint8_t *)"Waiting for the file to be sent ... (press 'a' to abort)\n\r");
  result = Ymodem_Receive(&size);
  if (result == COM_OK)
  {
    HAL_Delay(100);
    Serial_PutString((uint8_t *)"\n\n\r Programming Completed Successfully!\n\r--------------------------------\r\n Name: ");
    Serial_PutString(aFileName);
    Int2Str(number, size);
    Serial_PutString((uint8_t *)"\n\r Size: ");
    Serial_PutString(number);
    Serial_PutString((uint8_t *)" Bytes\r\n");
    Serial_PutString((uint8_t *)"-------------------\n");
  }
  else if (result == COM_LIMIT)
  {
    Serial_PutString((uint8_t *)"\n\n\rThe image size is higher than the allowed space memory!\n\r");
  }
  else if (result == COM_DATA)
  {
    Serial_PutString((uint8_t *)"\n\n\rVerification failed!\n\r");
  }
  else if (result == COM_ABORT)
  {
    Serial_PutString((uint8_t *)"\r\n\nAborted by user.\n\r");
  }
  else
  {
    Serial_PutString((uint8_t *)"\n\rFailed to receive the file!\n\r");
  }
}

/**
 * @brief  Upload a file via serial port.
 * @param  None
 * @retval None
 */
void SerialUpload(void)
{
  uint8_t status = 0;

  Serial_PutString((uint8_t *)"\n\n\rSelect Receive File\n\r");

  HAL_UART_Receive(&UartHandle, &status, 1, RX_TIMEOUT);
  if (status == CRC16)
  {
    /* Transmit the flash image through ymodem protocol */
    status = Ymodem_Transmit((uint8_t *)APPLICATION_ADDRESS, (const uint8_t *)"UploadedFlashImage.bin", USER_FLASH_SIZE);

    if (status != 0)
    {
      Serial_PutString((uint8_t *)"\n\rError Occurred while Transmitting File\n\r");
    }
    else
    {
      Serial_PutString((uint8_t *)"\n\rFile uploaded successfully \n\r");
    }
  }
}

static void scan_sd_card_hdiff_files(void)
{
  DIR dir;
  FILINFO fno;
  FRESULT res;

  hdiff_count = 0;

  res = f_opendir(&dir, "0:/");
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"\r\nError: Cannot open SD card directory!\r\n");
    return;
  }

  while (hdiff_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
    {
      break;
    }

    if (!(fno.fattrib & AM_DIR))
    {
      size_t len = strlen(fno.fname);
      if (len >= 6)
      {
        const char *ext = fno.fname + len - 6;
        if (strcmp(ext, ".hdiff") == 0 || strcmp(ext, ".HDIFF") == 0)
        {
          strncpy(hdiff_list[hdiff_count], fno.fname, MAX_FILENAME_LEN - 1);
          hdiff_list[hdiff_count][MAX_FILENAME_LEN - 1] = '\0';
          hdiff_count++;
        }
      }
    }
  }

  f_closedir(&dir);
}

static void HPatchUpgradeSDCard(void)
{
  uint8_t key = 0;
  uint8_t selected_diff = 0;
  uint8_t selected_old = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  hpatch_upgrade_err_t patch_result;
  hpatch_config_t config;
  char out_path[HPATCH_MAX_PATH_LEN];
  char *dot_pos;
  const char *upgrade_tag = "_HdiffUpgraded";

  Serial_PutString((uint8_t *)"\r\nInitializing TF card...\r\n");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"Error: SD card mount failed! Error code: ");
    Int2Str((uint8_t *)msg, res);
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"\r\nScanning for .hdiff files...\r\n\r\n");

  scan_sd_card_hdiff_files();

  if (hdiff_count == 0)
  {
    Serial_PutString((uint8_t *)"No .hdiff files found on SD card!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Found .hdiff files:\r\n");

  for (i = 0; i < hdiff_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, hdiff_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), hdiff_count, "\r\nSelect .hdiff file");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected_diff = key - '0';
      if (selected_diff >= 1 && selected_diff <= hdiff_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", hdiff_list[selected_diff - 1]);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nScanning for .bin firmware files...\r\n\r\n");

  scan_sd_card_files_filter(1);

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No .bin files found on SD card!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  Serial_PutString((uint8_t *)"Found firmware files:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nSelect old firmware file to update");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected_old = key - '0';
      if (selected_old >= 1 && selected_old <= file_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected_old - 1]);
  Serial_PutString((uint8_t *)msg);

  snprintf(config.diff_path, sizeof(config.diff_path), "0:/%s", hdiff_list[selected_diff - 1]);
  snprintf(config.old_path, sizeof(config.old_path), "0:/%s", file_list[selected_old - 1]);

  strncpy(out_path, config.old_path, sizeof(out_path) - 1);
  out_path[sizeof(out_path) - 1] = '\0';

  dot_pos = strrchr(out_path, '.');
  if (dot_pos != NULL)
  {
    size_t base_len = dot_pos - out_path;
    if (base_len + strlen(upgrade_tag) + 4 < sizeof(out_path))
    {
      snprintf(dot_pos, sizeof(out_path) - base_len, "%s.bin", upgrade_tag);
    }
    else
    {
      strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    }
  }
  else
  {
    strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    strncat(out_path, ".bin", sizeof(out_path) - strlen(out_path) - 1);
  }

  snprintf(config.out_path, sizeof(config.out_path), "%s", out_path);
  config.fatfs = &SDFatFS;

  snprintf(msg, sizeof(msg), "\r\nDiff file: %s\r\n", config.diff_path);
  Serial_PutString((uint8_t *)msg);
  snprintf(msg, sizeof(msg), "Old firmware: %s\r\n", config.old_path);
  Serial_PutString((uint8_t *)msg);
  snprintf(msg, sizeof(msg), "Output file: %s\r\n", config.out_path);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStarting HPatch differential upgrade...\r\n");

  patch_result = hpatch_upgrade_fatfs(&config);

  if (patch_result == HPATCH_OK)
  {
    Serial_PutString((uint8_t *)"\r\nHPatch upgrade completed successfully!\r\n");
    Serial_PutString((uint8_t *)"Upgraded firmware saved to SD card.\r\n");
    snprintf(msg, sizeof(msg), "Output: %s\r\n", config.out_path);
    Serial_PutString((uint8_t *)msg);
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nHPatch upgrade failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-patch_result));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");

    switch (patch_result)
    {
    case HPATCH_ERR_MOUNT:
      Serial_PutString((uint8_t *)"Detail: SD card mount error\r\n");
      break;
    case HPATCH_ERR_OPEN_DIFF:
      Serial_PutString((uint8_t *)"Detail: Cannot open .hdiff file\r\n");
      break;
    case HPATCH_ERR_OPEN_OLD:
      Serial_PutString((uint8_t *)"Detail: Cannot open old firmware file\r\n");
      break;
    case HPATCH_ERR_OPEN_OUT:
      Serial_PutString((uint8_t *)"Detail: Cannot create output file\r\n");
      break;
    case HPATCH_ERR_READ_DIFF:
      Serial_PutString((uint8_t *)"Detail: Error reading diff data\r\n");
      break;
    case HPATCH_ERR_READ_OLD:
      Serial_PutString((uint8_t *)"Detail: Error reading old firmware\r\n");
      break;
    case HPATCH_ERR_WRITE:
      Serial_PutString((uint8_t *)"Detail: Error writing output\r\n");
      break;
    case HPATCH_ERR_DECOMPRESS:
      Serial_PutString((uint8_t *)"Detail: Decompression error\r\n");
      break;
    case HPATCH_ERR_PATCH:
      Serial_PutString((uint8_t *)"Detail: Patch application error\r\n");
      break;
    case HPATCH_ERR_INVALID_HEAD:
      Serial_PutString((uint8_t *)"Detail: Invalid diff file header\r\n");
      break;
    case HPATCH_ERR_MEMORY:
      Serial_PutString((uint8_t *)"Detail: Insufficient memory\r\n");
      break;
    default:
      Serial_PutString((uint8_t *)"Detail: Unknown error\r\n");
      break;
    }
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void HPatchUpgradeLFS(void)
{
  uint8_t key = 0;
  uint8_t selected_diff = 0;
  uint8_t selected_old = 0;
  uint8_t i;
  char msg[256];
  int res;
  lfs_t lfs;
  hpatch_upgrade_err_t patch_result;
  hpatch_lfs_config_t config;
  char out_path[HPATCH_MAX_PATH_LEN];
  char *dot_pos;
  const char *upgrade_tag = "_HdiffUpgraded";

  Serial_PutString((uint8_t *)"\r\nInitializing SPI Flash...\r\n");

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    Serial_PutString((uint8_t *)"Error: SPI Flash initialization failed!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Mounting LittleFS...\r\n");

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    Serial_PutString((uint8_t *)"Error: LittleFS mount failed!\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"\r\nScanning for .hdiff files...\r\n\r\n");

  scan_lfs_hdiff_files(&lfs);

  if (hdiff_count == 0)
  {
    Serial_PutString((uint8_t *)"No .hdiff files found on SPI Flash!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  Serial_PutString((uint8_t *)"Found .hdiff files:\r\n");

  for (i = 0; i < hdiff_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, hdiff_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), hdiff_count, "\r\nSelect .hdiff file");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected_diff = key - '0';
      if (selected_diff >= 1 && selected_diff <= hdiff_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", hdiff_list[selected_diff - 1]);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nScanning for .bin firmware files...\r\n\r\n");

  scan_lfs_files_filter(&lfs, 1);

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No .bin files found on SPI Flash!\r\n");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  Serial_PutString((uint8_t *)"Found firmware files:\r\n");

  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s\r\n", i + 1, file_list[i]);
    Serial_PutString((uint8_t *)msg);
  }

  BuildSelectionPrompt(msg, sizeof(msg), file_count, "\r\nSelect old firmware file to update");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected_old = key - '0';
      if (selected_old >= 1 && selected_old <= file_count)
      {
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected_old - 1]);
  Serial_PutString((uint8_t *)msg);

  strncpy(out_path, file_list[selected_old - 1], sizeof(out_path) - 1);
  out_path[sizeof(out_path) - 1] = '\0';

  dot_pos = strrchr(out_path, '.');
  if (dot_pos != NULL)
  {
    size_t base_len = dot_pos - out_path;
    if (base_len + strlen(upgrade_tag) + 4 < sizeof(out_path))
    {
      snprintf(dot_pos, sizeof(out_path) - base_len, "%s.bin", upgrade_tag);
    }
    else
    {
      strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    }
  }
  else
  {
    strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    strncat(out_path, ".bin", sizeof(out_path) - strlen(out_path) - 1);
  }

  config.lfs = &lfs;
  config.diff_path = hdiff_list[selected_diff - 1];
  config.old_path = file_list[selected_old - 1];
  config.out_path = out_path;

  snprintf(msg, sizeof(msg), "\r\nDiff file: %s\r\n", config.diff_path);
  Serial_PutString((uint8_t *)msg);
  snprintf(msg, sizeof(msg), "Old firmware: %s\r\n", config.old_path);
  Serial_PutString((uint8_t *)msg);
  snprintf(msg, sizeof(msg), "Output file: %s\r\n", config.out_path);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStarting HPatch differential upgrade...\r\n");

  patch_result = hpatch_upgrade_lfs(&config);

  if (patch_result == HPATCH_OK)
  {
    Serial_PutString((uint8_t *)"\r\nHPatch upgrade completed successfully!\r\n");
    Serial_PutString((uint8_t *)"Upgraded firmware saved to SPI Flash.\r\n");
    snprintf(msg, sizeof(msg), "Output: %s\r\n", config.out_path);
    Serial_PutString((uint8_t *)msg);
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nHPatch upgrade failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-patch_result));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");

    switch (patch_result)
    {
    case HPATCH_ERR_OPEN_DIFF:
      Serial_PutString((uint8_t *)"Detail: Cannot open .hdiff file\r\n");
      break;
    case HPATCH_ERR_OPEN_OLD:
      Serial_PutString((uint8_t *)"Detail: Cannot open old firmware file\r\n");
      break;
    case HPATCH_ERR_OPEN_OUT:
      Serial_PutString((uint8_t *)"Detail: Cannot create output file\r\n");
      break;
    case HPATCH_ERR_READ_DIFF:
      Serial_PutString((uint8_t *)"Detail: Error reading diff data\r\n");
      break;
    case HPATCH_ERR_READ_OLD:
      Serial_PutString((uint8_t *)"Detail: Error reading old firmware\r\n");
      break;
    case HPATCH_ERR_WRITE:
      Serial_PutString((uint8_t *)"Detail: Error writing output\r\n");
      break;
    case HPATCH_ERR_DECOMPRESS:
      Serial_PutString((uint8_t *)"Detail: Decompression error\r\n");
      break;
    case HPATCH_ERR_PATCH:
      Serial_PutString((uint8_t *)"Detail: Patch application error\r\n");
      break;
    case HPATCH_ERR_INVALID_HEAD:
      Serial_PutString((uint8_t *)"Detail: Invalid diff file header\r\n");
      break;
    case HPATCH_ERR_MEMORY:
      Serial_PutString((uint8_t *)"Detail: Insufficient memory\r\n");
      break;
    default:
      Serial_PutString((uint8_t *)"Detail: Unknown error\r\n");
      break;
    }
  }

  lfs_spi_flash_unmount(&lfs);
}

static void HPatchUpgradeMenu(void)
{
  uint8_t key = 0;

  while (1)
  {
    Serial_PutString((uint8_t *)"\r\n============== HPatch Differential Upgrade Menu ==============\r\n\n");
    Serial_PutString((uint8_t *)"  HPatch upgrade from SD card -------------------------- 1\r\n\n");
    Serial_PutString((uint8_t *)"  HPatch upgrade from SPI Flash ------------------------ 2\r\n\n");
    Serial_PutString((uint8_t *)"  Return to Main Menu ---------------------------------- 0\r\n\n");
    Serial_PutString((uint8_t *)"===============================================================\r\n\n");

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '0':
      Serial_PutString((uint8_t *)"\r\nReturn to Main Menu...\r\n");
      return;
    case '1':
      HPatchUpgradeSDCard();
      break;
    case '2':
      HPatchUpgradeLFS();
      break;
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 0, 1 or 2\r");
      break;
    }
  }
}

static void UART_Passthrough(void)
{
  uint8_t rx_data;
  uint8_t q_count = 0;
  uint32_t q_timer = 0;

  Serial_PutString((uint8_t *)"\r\n========== UART4 <-> USART1 Passthrough Mode ==========\r\n");
  Serial_PutString((uint8_t *)"  UART4 (PA0/PA1) <--> USART1 (PA9/PA10)\r\n");
  Serial_PutString((uint8_t *)"  Baud Rate: 115200, 8N1\r\n");
  Serial_PutString((uint8_t *)"  For ESP8266/BLE AT Command Debug\r\n");
  Serial_PutString((uint8_t *)"  Press 'q' 3 times within 1 second to exit\r\n");
  Serial_PutString((uint8_t *)"========================================================\r\n\n");

  HAL_UART_AbortReceive_IT(&huart1);
  __HAL_UART_DISABLE_IT(&huart1, UART_IT_RXNE);
  __HAL_UART_DISABLE_IT(&huart1, UART_IT_IDLE);

  __HAL_UART_FLUSH_DRREGISTER(&huart4);
  __HAL_UART_FLUSH_DRREGISTER(&huart1);

  __HAL_UART_CLEAR_OREFLAG(&huart4);
  __HAL_UART_CLEAR_OREFLAG(&huart1);

  while (1)
  {
    if ((huart4.Instance->SR & UART_FLAG_RXNE) != RESET)
    {
      rx_data = (uint8_t)(huart4.Instance->DR & 0xFF);

      if (rx_data == 'q')
      {
        if (q_count == 0 || (HAL_GetTick() - q_timer) < 1000)
        {
          q_count++;
          q_timer = HAL_GetTick();
          if (q_count >= 3)
          {
            Serial_PutString((uint8_t *)"\r\n\nExiting passthrough mode...\r\n");
            __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
            __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
            return;
          }
        }
        else
        {
          q_count = 1;
          q_timer = HAL_GetTick();
        }
        while ((huart1.Instance->SR & UART_FLAG_TXE) == RESET)
          ;
        huart1.Instance->DR = rx_data;
      }
      else
      {
        q_count = 0;
        while ((huart1.Instance->SR & UART_FLAG_TXE) == RESET)
          ;
        huart1.Instance->DR = rx_data;
      }
    }
    else if ((huart4.Instance->SR & UART_FLAG_ORE) != RESET)
    {
      __HAL_UART_CLEAR_OREFLAG(&huart4);
    }

    if ((huart1.Instance->SR & UART_FLAG_RXNE) != RESET)
    {
      rx_data = (uint8_t)(huart1.Instance->DR & 0xFF);
      while ((huart4.Instance->SR & UART_FLAG_TXE) == RESET)
        ;
      huart4.Instance->DR = rx_data;
    }
    else if ((huart1.Instance->SR & UART_FLAG_ORE) != RESET)
    {
      __HAL_UART_CLEAR_OREFLAG(&huart1);
    }
  }
}

static void Print_Current_Time(void)
{
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  char msg[128];

  if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Serial_PutString((uint8_t *)"\r\nFailed to get RTC time!\r\n");
    return;
  }
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  if (sDate.Year < 25)
  {
    Serial_PutString((uint8_t *)"\r\nRTC time not set (year < 2025)\r\n");
    return;
  }

  uint16_t year = sDate.Year + 2000;
  uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
  static const uint16_t days_before_month[2][12] = {
      {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
      {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};

  uint32_t days = 0;
  for (uint16_t y = 1970; y < year; y++)
  {
    days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
  }
  days += days_before_month[is_leap][sDate.Month - 1];
  days += sDate.Date - 1;

  uint32_t timestamp = days * 86400UL + sTime.Hours * 3600UL + sTime.Minutes * 60UL + sTime.Seconds;

  snprintf(msg, sizeof(msg), "\r\nCurrent Time: %04d-%02d-%02d %02d:%02d:%02d (UTC+8)\r\n",
           year, sDate.Month, sDate.Date, sTime.Hours, sTime.Minutes, sTime.Seconds);
  Serial_PutString((uint8_t *)msg);

  snprintf(msg, sizeof(msg), "Unix Timestamp: %lu\r\n", (unsigned long)timestamp);
  Serial_PutString((uint8_t *)msg);
}

static void ESP8266_TestMenu(void)
{
  uint8_t key = 0;
  char msg[128];

  while (1)
  {
    Serial_PutString((uint8_t *)"\r\n============== ESP8266 Test Menu ==============\r\n\n");
    Serial_PutString((uint8_t *)"  WiFi Init & Connect AP  ---------------------------- 1\r\n\n");
    Serial_PutString((uint8_t *)"  AT Command Test  ----------------------------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  TCP Connect Test  ---------------------------------- 3\r\n\n");
    Serial_PutString((uint8_t *)"  Enter Transparent Mode  ---------------------------- 4\r\n\n");
    Serial_PutString((uint8_t *)"  Exit Transparent Mode  ----------------------------- 5\r\n\n");
    Serial_PutString((uint8_t *)"  Set OTA Target: Internal Flash --------------------- 6\r\n\n");
    Serial_PutString((uint8_t *)"  Set OTA Target: SD Card (FATFS) -------------------- 7\r\n\n");
    Serial_PutString((uint8_t *)"  Set OTA Target: SPI Flash (LFS) -------------------- 8\r\n\n");
    Serial_PutString((uint8_t *)"  OneNET OTA Download  ------------------------------- 9\r\n\n");
    Serial_PutString((uint8_t *)"  Show Current Time  --------------------------------- a\r\n\n");
    Serial_PutString((uint8_t *)"  MQTT Test Menu  ------------------------------------ b\r\n\n");
    Serial_PutString((uint8_t *)"  Return to Main Menu  ------------------------------- 0\r\n\n");
    Serial_PutString((uint8_t *)"================================================\r\n\n");

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '0':
      Serial_PutString((uint8_t *)"\r\nReturn to Main Menu...\r\n");
      return;

    case '1':
      Serial_PutString((uint8_t *)"\r\nInitializing ESP8266 and connecting to AP...\r\n");
      esp8266_ota_init();
      break;

    case '2':
      Serial_PutString((uint8_t *)"\r\nTesting AT command...\r\n");
      if (esp8266_at_test() == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"AT test OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"AT test FAILED!\r\n");
      }
      break;

    case '3':
    {
      char server_ip[64];
      char server_port[8];
      uint8_t idx = 0;

      Serial_PutString((uint8_t *)"\r\nEnter server IP: ");
      idx = 0;
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          server_ip[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(server_ip) - 1 && key >= 0x20 && key <= 0x7E)
        {
          server_ip[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }

      Serial_PutString((uint8_t *)"\r\nEnter server port: ");
      idx = 0;
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          server_port[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(server_port) - 1 && key >= '0' && key <= '9')
        {
          server_port[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }

      snprintf(msg, sizeof(msg), "\r\nConnecting to %s:%s...\r\n", server_ip, server_port);
      Serial_PutString((uint8_t *)msg);

      if (esp8266_connect_tcp_server(server_ip, server_port) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"TCP connect OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"TCP connect FAILED!\r\n");
      }
      break;
    }

    case '4':
      Serial_PutString((uint8_t *)"\r\nEntering transparent mode...\r\n");
      if (esp8266_enter_unvarnished() == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Transparent mode enabled!\r\n");
        Serial_PutString((uint8_t *)"You can now send data directly.\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Enter transparent mode FAILED!\r\n");
      }
      break;

    case '5':
      Serial_PutString((uint8_t *)"\r\nExiting transparent mode...\r\n");
      esp8266_exit_unvarnished();
      Serial_PutString((uint8_t *)"Transparent mode exited.\r\n");
      break;

    case '6':
      Serial_PutString((uint8_t *)"\r\nSetting OTA target to Internal Flash...\r\n");
      ONENET_OTA_SetTargetType(0);
      break;

    case '7':
      Serial_PutString((uint8_t *)"\r\nSetting OTA target to SD Card (FATFS)...\r\n");
      ONENET_OTA_SetTargetType(1);
      break;

    case '8':
      Serial_PutString((uint8_t *)"\r\nSetting OTA target to SPI Flash (LFS)...\r\n");
      ONENET_OTA_SetTargetType(2);
      break;

    case '9':
      Serial_PutString((uint8_t *)"\r\nStarting OneNET OTA download...\r\n");
      ONENET_OTA_ProcessUpgrade();
      break;

    case 'a':
    case 'A':
      Print_Current_Time();
      break;

    case 'b':
    case 'B':
      MQTT_TestMenu();
      break;

    default:
      Serial_PutString((uint8_t *)"Invalid Number! ==> The number should be 0-9 or a-b\r");
      break;
    }
  }
}

static void MQTT_TestMenu(void)
{
  uint8_t key = 0;
  char msg[256];
  char mqtt_cmd[256];
  static uint8_t mqtt_connected = 0;

  while (1)
  {
    Serial_PutString((uint8_t *)"\r\n============== MQTT Test Menu ==============\r\n\n");
    Serial_PutString((uint8_t *)"  Check MQTT Connection Status  ---------------------- 1\r\n\n");
    Serial_PutString((uint8_t *)"  Configure MQTT User  ------------------------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  Connect to MQTT Server  ---------------------------- 3\r\n\n");
    Serial_PutString((uint8_t *)"  Subscribe Property Topics  ------------------------- 4\r\n\n");
    Serial_PutString((uint8_t *)"  Publish Property  ---------------------------------- 5\r\n\n");
    Serial_PutString((uint8_t *)"  Listen & Auto Reply Property Set  ------------------ 6\r\n\n");
    Serial_PutString((uint8_t *)"  Disconnect MQTT  ----------------------------------- 7\r\n\n");
    Serial_PutString((uint8_t *)"  Sync Time (SNTP)  ---------------------------------- 8\r\n\n");
    Serial_PutString((uint8_t *)"  Publish RTC Time (1s interval)  -------------------- 9\r\n\n");
    Serial_PutString((uint8_t *)"  Return to ESP8266 Menu  ---------------------------- 0\r\n\n");
    Serial_PutString((uint8_t *)"==============================================\r\n\n");

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '0':
      Serial_PutString((uint8_t *)"\r\nReturn to ESP8266 Menu...\r\n");
      return;

    case '1':
    {
      Serial_PutString((uint8_t *)"\r\nChecking MQTT connection status...\r\n");
      snprintf(mqtt_cmd, sizeof(mqtt_cmd), "AT+MQTTCONN?");
      esp8266_uart_rx_restart();
      esp8266_uart_printf("%s\r\n", mqtt_cmd);

      uint32_t timeout = 2000;
      uint8_t *resp = NULL;
      while (timeout > 0)
      {
        resp = esp8266_uart_rx_get_frame();
        if (resp != NULL)
        {
          if (strstr((const char *)resp, "+MQTTCONN:") != NULL)
          {
            char *conn_line = strstr((const char *)resp, "+MQTTCONN:");
            if (conn_line != NULL)
            {
              char *server_start = strstr(conn_line, ",\"");
              if (server_start != NULL)
              {
                server_start += 2;
                if (*server_start == '"')
                {
                  Serial_PutString((uint8_t *)"MQTT: Not connected (server is empty)\r\n");
                  mqtt_connected = 0;
                }
                else
                {
                  Serial_PutString((uint8_t *)"MQTT: Connected\r\n");
                  Serial_PutString((uint8_t *)conn_line);
                  Serial_PutString((uint8_t *)"\r\n");
                  mqtt_connected = 1;
                }
              }
              else
              {
                Serial_PutString((uint8_t *)"MQTT: Not connected\r\n");
                mqtt_connected = 0;
              }
            }
            break;
          }
          else if (strstr((const char *)resp, "ERROR") != NULL)
          {
            Serial_PutString((uint8_t *)"MQTT: Not configured\r\n");
            mqtt_connected = 0;
            break;
          }
          esp8266_uart_rx_restart();
        }
        timeout--;
        HAL_Delay(1);
      }
      if (timeout == 0)
      {
        Serial_PutString((uint8_t *)"MQTT: Check timeout\r\n");
      }
      break;
    }

    case '2':
    {
      mqtt_user_config_t config;
      memset(&config, 0, sizeof(config));

      strncpy(config.client_id, ONENET_DEVICE_NAME, sizeof(config.client_id) - 1);
      strncpy(config.username, ONENET_PRODUCT_ID, sizeof(config.username) - 1);

      Serial_PutString((uint8_t *)"\r\nPress '!' to use macro defaults, or Enter to customize...\r\n");
      snprintf(msg, sizeof(msg), "Default: client=%s, user=%s\r\n", config.client_id, config.username);
      Serial_PutString((uint8_t *)msg);

      Serial_PutString((uint8_t *)"\r\nEnter Client ID (or Enter for default): ");
      uint8_t idx = 0;
      char input_buf[256];
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          input_buf[idx] = '\0';
          break;
        }
        else if (key == '!' && idx == 0)
        {
          input_buf[0] = '!';
          input_buf[1] = '\0';
          Serial_PutString((uint8_t *)"!");
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
        {
          input_buf[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }

      if (strcmp(input_buf, "!") == 0)
      {
        Serial_PutString((uint8_t *)"\r\nUsing macro defaults...\r\n");
        strncpy(config.client_id, ONENET_DEVICE_NAME, sizeof(config.client_id) - 1);
        strncpy(config.username, ONENET_PRODUCT_ID, sizeof(config.username) - 1);
        strncpy(config.password, ONENET_MQTT_TOKEN, sizeof(config.password) - 1);
      }
      else
      {
        if (idx > 0)
        {
          strncpy(config.client_id, input_buf, sizeof(config.client_id) - 1);
        }

        Serial_PutString((uint8_t *)"\r\nEnter Username (Product ID): ");
        idx = 0;
        __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
        while (1)
        {
          HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
          if (key == '\r' || key == '\n')
          {
            input_buf[idx] = '\0';
            break;
          }
          else if (key == 0x08 || key == 0x7F)
          {
            if (idx > 0)
            {
              idx--;
              Serial_PutString((uint8_t *)"\b \b");
            }
          }
          else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
          {
            input_buf[idx++] = key;
            HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
          }
        }
        if (idx > 0)
        {
          strncpy(config.username, input_buf, sizeof(config.username) - 1);
        }

        Serial_PutString((uint8_t *)"\r\nEnter Password (Token): ");
        idx = 0;
        __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
        while (1)
        {
          HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
          if (key == '\r' || key == '\n')
          {
            input_buf[idx] = '\0';
            break;
          }
          else if (key == 0x08 || key == 0x7F)
          {
            if (idx > 0)
            {
              idx--;
              Serial_PutString((uint8_t *)"\b \b");
            }
          }
          else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
          {
            input_buf[idx++] = key;
            HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
          }
        }
        if (idx > 0)
        {
          strncpy(config.password, input_buf, sizeof(config.password) - 1);
        }
      }

      snprintf(msg, sizeof(msg), "\r\nConfiguring MQTT user: client=%s, user=%s\r\n",
               config.client_id, config.username);
      Serial_PutString((uint8_t *)msg);

      if (esp8266_mqtt_usercfg(0, &config) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"MQTT user config OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"MQTT user config FAILED!\r\n");
      }
      break;
    }

    case '3':
    {
      Serial_PutString((uint8_t *)"\r\nChecking if already connected...\r\n");

      snprintf(mqtt_cmd, sizeof(mqtt_cmd), "AT+MQTTCONN?");
      esp8266_uart_rx_restart();
      esp8266_uart_printf("%s\r\n", mqtt_cmd);

      uint32_t timeout = 2000;
      uint8_t *resp = NULL;
      uint8_t already_connected = 0;

      while (timeout > 0)
      {
        resp = esp8266_uart_rx_get_frame();
        if (resp != NULL)
        {
          if (strstr((const char *)resp, "+MQTTCONN:") != NULL)
          {
            char *conn_line = strstr((const char *)resp, "+MQTTCONN:");
            if (conn_line != NULL)
            {
              char *server_start = strstr(conn_line, ",\"");
              if (server_start != NULL)
              {
                server_start += 2;
                if (*server_start != '"')
                {
                  Serial_PutString((uint8_t *)"MQTT: Already connected!\r\n");
                  Serial_PutString((uint8_t *)conn_line);
                  Serial_PutString((uint8_t *)"\r\n");
                  mqtt_connected = 1;
                  already_connected = 1;
                }
              }
            }
            break;
          }
          esp8266_uart_rx_restart();
        }
        timeout--;
        HAL_Delay(1);
      }

      if (already_connected)
      {
        break;
      }

      Serial_PutString((uint8_t *)"\r\nConnecting to MQTT server...\r\n");

      char host[64] = ONENET_MQTT_HOST;
      uint16_t port = ONENET_MQTT_PORT;

      snprintf(msg, sizeof(msg), "\r\nEnter MQTT server host (default: %s): ", host);
      Serial_PutString((uint8_t *)msg);
      uint8_t idx = 0;
      char input_buf[128];
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          input_buf[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
        {
          input_buf[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }
      if (idx > 0)
      {
        strncpy(host, input_buf, sizeof(host) - 1);
      }

      if (esp8266_mqtt_connect(0, host, port, 1) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"MQTT connect OK!\r\n");
        mqtt_connected = 1;
      }
      else
      {
        Serial_PutString((uint8_t *)"MQTT connect FAILED!\r\n");
      }
      break;
    }

    case '4':
    {
      Serial_PutString((uint8_t *)"\r\nSubscribing to property topics...\r\n");

      char topic[128];

      // 设备属性上报响应
      snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post/reply",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }

      // 设备属性设置请求
      snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }

      // 设备获取属性期望值响应
      snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/get/reply",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }

      // 设备清除属性期望值响应
      snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/delete/reply",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }

      // 设备属性获取请求
      snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/get",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }

      // 系统OTA升级通知
      snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform",
               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
      snprintf(msg, sizeof(msg), "Subscribing: %s\r\n", topic);
      Serial_PutString((uint8_t *)msg);
      if (esp8266_mqtt_subscribe(0, topic, 0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Subscribe OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Subscribe FAILED!\r\n");
      }
      break;
    }

    case '5':
    {
      Serial_PutString((uint8_t *)"\r\nPublishing single property...\r\n");

      mqtt_property_t prop;
      memset(&prop, 0, sizeof(prop));

      Serial_PutString((uint8_t *)"\r\nEnter message ID (default: 007): ");
      uint8_t idx = 0;
      char msg_id[32] = "007";
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          msg_id[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(msg_id) - 1 && key >= 0x20 && key <= 0x7E)
        {
          msg_id[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }
      if (idx == 0)
      {
        strcpy(msg_id, "007");
      }

      Serial_PutString((uint8_t *)"\r\nEnter property key (e.g. BSP_LED): ");
      idx = 0;
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          prop.key[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(prop.key) - 1 && key >= 0x20 && key <= 0x7E)
        {
          prop.key[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }

      Serial_PutString((uint8_t *)"\r\nEnter value type (0=int, 1=float, 2=bool, 3=string): ");
      idx = 0;
      char type_buf[4];
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          type_buf[idx] = '\0';
          break;
        }
        else if (key >= '0' && key <= '9' && idx < sizeof(type_buf) - 1)
        {
          type_buf[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }
      prop.value_type = (uint8_t)atoi(type_buf);

      Serial_PutString((uint8_t *)"\r\nEnter value: ");
      idx = 0;
      char value_buf[64];
      __HAL_UART_FLUSH_DRREGISTER(&UartHandle);
      while (1)
      {
        HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);
        if (key == '\r' || key == '\n')
        {
          value_buf[idx] = '\0';
          break;
        }
        else if (key == 0x08 || key == 0x7F)
        {
          if (idx > 0)
          {
            idx--;
            Serial_PutString((uint8_t *)"\b \b");
          }
        }
        else if (idx < sizeof(value_buf) - 1 && key >= 0x20 && key <= 0x7E)
        {
          value_buf[idx++] = key;
          HAL_UART_Transmit(&UartHandle, &key, 1, HAL_MAX_DELAY);
        }
      }

      if (prop.value_type == MQTT_VALUE_TYPE_FLOAT)
      {
        prop.value_float = atof(value_buf);
      }
      else if (prop.value_type == MQTT_VALUE_TYPE_BOOL)
      {
        if ((tolower(value_buf[0]) == 't' && tolower(value_buf[1]) == 'r' &&
             tolower(value_buf[2]) == 'u' && tolower(value_buf[3]) == 'e') ||
            strcmp(value_buf, "1") == 0)
        {
          prop.value_int = 1;
        }
        else
        {
          prop.value_int = 0;
        }
      }
      else if (prop.value_type == MQTT_VALUE_TYPE_STRING)
      {
        strncpy(prop.id, value_buf, sizeof(prop.id) - 1);
      }
      else
      {
        prop.value_int = atoi(value_buf);
      }

      snprintf(msg, sizeof(msg), "\r\nPublishing: %s = %s (type=%d, id=%s)\r\n",
               prop.key, value_buf, prop.value_type, msg_id);
      Serial_PutString((uint8_t *)msg);

      if (esp8266_mqtt_publish_property(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                        &prop, 1, msg_id) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"Publish OK!\r\n");
      }
      else
      {
        Serial_PutString((uint8_t *)"Publish FAILED!\r\n");
      }
      break;
    }

    case '6':
    {
      Serial_PutString((uint8_t *)"\r\nListening for property set commands...\r\n");
      Serial_PutString((uint8_t *)"Press 'q' 3 times within 1 second to exit\r\n");

      char recv_topic[128];
      char recv_payload[512];
      char recv_msg_id[32];
      uint8_t exit_q_count = 0;
      uint32_t exit_q_timer = 0;

      while (1)
      {
        if ((huart4.Instance->SR & UART_FLAG_RXNE) != RESET)
        {
          uint8_t rx_char = (uint8_t)(huart4.Instance->DR & 0xFF);
          if (rx_char == 'q')
          {
            if (exit_q_count == 0 || (HAL_GetTick() - exit_q_timer) < 1000)
            {
              exit_q_count++;
              exit_q_timer = HAL_GetTick();
              if (exit_q_count >= 3)
              {
                Serial_PutString((uint8_t *)"\r\nExiting listen mode...\r\n");
                break;
              }
            }
            else
            {
              exit_q_count = 1;
              exit_q_timer = HAL_GetTick();
            }
          }
          else
          {
            exit_q_count = 0;
          }
        }

        uint8_t *resp = esp8266_uart_rx_get_frame();
        if (resp != NULL)
        {
          if (strstr((const char *)resp, "+MQTTSUBRECV:") != NULL)
          {
            memset(recv_topic, 0, sizeof(recv_topic));
            memset(recv_payload, 0, sizeof(recv_payload));
            memset(recv_msg_id, 0, sizeof(recv_msg_id));

            if (esp8266_mqtt_check_property_set_recv(recv_topic, recv_payload, sizeof(recv_payload), recv_msg_id) == ESP8266_EOK)
            {
              snprintf(msg, sizeof(msg), "\r\nReceived: topic=%s\r\n", recv_topic);
              Serial_PutString((uint8_t *)msg);
              snprintf(msg, sizeof(msg), "Payload: %s\r\n", recv_payload);
              Serial_PutString((uint8_t *)msg);
              snprintf(msg, sizeof(msg), "Message ID: %s\r\n", recv_msg_id);
              Serial_PutString((uint8_t *)msg);

              if (strstr(recv_topic, "/thing/property/set") != NULL)
              {
                Serial_PutString((uint8_t *)"Auto replying to property set...\r\n");

                if (esp8266_mqtt_publish_set_reply(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                                   recv_msg_id, 200, "user_succ") == ESP8266_EOK)
                {
                  Serial_PutString((uint8_t *)"Reply sent successfully!\r\n");
                }
                else
                {
                  Serial_PutString((uint8_t *)"Reply FAILED!\r\n");
                }
              }
            }
          }
          esp8266_uart_rx_restart();
        }
        HAL_Delay(10);
      }
      break;
    }

    case '7':
      Serial_PutString((uint8_t *)"\r\nDisconnecting MQTT...\r\n");
      if (esp8266_mqtt_disconnect(0) == ESP8266_EOK)
      {
        Serial_PutString((uint8_t *)"MQTT disconnected!\r\n");
        mqtt_connected = 0;
      }
      else
      {
        Serial_PutString((uint8_t *)"Disconnect FAILED!\r\n");
      }
      break;

    case '8':
      Serial_PutString((uint8_t *)"\r\nSyncing time from server...\r\n");
      if (ONENET_SyncTime())
      {
        Serial_PutString((uint8_t *)"Time sync success!\r\n");
        Print_Current_Time();
      }
      else
      {
        Serial_PutString((uint8_t *)"Time sync FAILED!\r\n");
      }
      break;

    case '9':
    {
      Serial_PutString((uint8_t *)"\r\nPublishing RTC time every 1s...\r\n");
      Serial_PutString((uint8_t *)"Press 'q' or 'Q' to exit\r\n");

      uint32_t publish_count = 0;
      char rtc_time_str[64];
      uint8_t key = 0;

      while (1)
      {
        if (HAL_UART_Receive(&UartHandle, &key, 1, 10) == HAL_OK)
        {
          if (key == 'q' || key == 'Q')
          {
            Serial_PutString((uint8_t *)"\r\nExiting RTC time publish mode...\r\n");
            snprintf(msg, sizeof(msg), "Total published: %lu times\r\n", (unsigned long)publish_count);
            Serial_PutString((uint8_t *)msg);
            break;
          }
        }

        RTC_TimeTypeDef sTime;
        RTC_DateTypeDef sDate;

        if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK)
        {
          HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

          uint16_t year = sDate.Year + 2000;
          snprintf(rtc_time_str, sizeof(rtc_time_str), "%04d-%02d-%02d %02d:%02d:%02d",
                   year, sDate.Month, sDate.Date, sTime.Hours, sTime.Minutes, sTime.Seconds);

          mqtt_property_t prop;
          memset(&prop, 0, sizeof(prop));
          strncpy(prop.key, "RTC_TIME", sizeof(prop.key) - 1);
          strncpy(prop.id, rtc_time_str, sizeof(prop.id) - 1);
          prop.value_type = MQTT_VALUE_TYPE_STRING;

          if (esp8266_mqtt_publish_property(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
                                            &prop, 1, "007") == ESP8266_EOK)
          {
            publish_count++;
            snprintf(msg, sizeof(msg), "Published %lu times: %s\r\n", (unsigned long)publish_count, rtc_time_str);
            Serial_PutString((uint8_t *)msg);
          }
        }

        HAL_Delay(1000);
      }
      break;
    }

    default:
      Serial_PutString((uint8_t *)"Invalid Number! ==> The number should be 0-9\r");
      break;
    }
  }
}

/**
 * @brief  Display the Main Menu on HyperTerminal
 * @param  None
 * @retval None
 */
void Main_Menu(void)
{
  uint8_t key = 0;

  Serial_PutString((uint8_t *)"\r\n======================================================================");
  Serial_PutString((uint8_t *)"\r\n=              (C) COPYRIGHT 2016 STMicroelectronics                 =");
  Serial_PutString((uint8_t *)"\r\n=                                                                    =");
  Serial_PutString((uint8_t *)"\r\n=          STM32F4xx In-Application Programming Application          =");
  Serial_PutString((uint8_t *)"\r\n=                                                                    =");
  Serial_PutString((uint8_t *)"\r\n=                       By MCD Application Team                      =");
  Serial_PutString((uint8_t *)"\r\n======================================================================");
  Serial_PutString((uint8_t *)"\r\n\r\n");

  while (1)
  {

    /* Test if any sector of Flash memory where user application will be loaded is write protected */
    FlashProtection = FLASH_If_GetWriteProtectionStatus();

    Serial_PutString((uint8_t *)"\r\n=================== Main Menu ============================\r\n\n");
    Serial_PutString((uint8_t *)"  Download image to internal Flash  -------------------- 1\r\n\n");
    Serial_PutString((uint8_t *)"  Upload image from internal Flash  -------------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  Store image to SPI-Flash LFS  ------------------------ 3\r\n\n");
    Serial_PutString((uint8_t *)"  Execute the loaded application  ---------------------- 4\r\n\n");

    if (FlashProtection != FLASHIF_PROTECTION_NONE)
    {
      Serial_PutString((uint8_t *)"  Disable Flash write protection  ---------------------- 5\r\n\n");
    }
    else
    {
      Serial_PutString((uint8_t *)"  Enable Flash write protection  ----------------------- 5\r\n\n");
    }

    Serial_PutString((uint8_t *)"  Decrypt and download encrypted firmware  ------------- 6\r\n\n");
    Serial_PutString((uint8_t *)"  Decrypt .bin.aes file on SD card  -------------------- 7\r\n\n");
    Serial_PutString((uint8_t *)"  HPatch differential upgrade  ------------------------- 8\r\n\n");
    Serial_PutString((uint8_t *)"  UART4 <-> USART1 Passthrough  ------------------------ 9\r\n\n");
    Serial_PutString((uint8_t *)"  ESP8266 WiFi & OTA Test  ----------------------------- a\r\n\n");

    Serial_PutString((uint8_t *)"==========================================================\r\n\n");

    /* Clean the input path */
    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    /* Receive key */
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '1':
      ImageDownloadMenu();
      break;
    case '2':
      SerialUpload();
      break;
    case '3':
      StoreImageMenu();
      break;
    case '4':
      Serial_PutString((uint8_t *)"Start program execution......\r\n\n");
      bootloader_ctx.config.jump.jump_func(bootloader_ctx.config.jump.app_jump_addr);
      break;
    case '5':
      if (FlashProtection != FLASHIF_PROTECTION_NONE)
      {
        if (FLASH_If_WriteProtectionConfig(OB_WRPSTATE_DISABLE) == HAL_OK)
        {
          Serial_PutString((uint8_t *)"Write Protection disabled...\r\n");
          Serial_PutString((uint8_t *)"System will now restart...\r\n");
          HAL_FLASH_OB_Launch();
          HAL_FLASH_Unlock();
        }
        else
        {
          Serial_PutString((uint8_t *)"Error: Flash write un-protection failed...\r\n");
        }
      }
      else
      {
        if (FLASH_If_WriteProtectionConfig(OB_WRPSTATE_ENABLE) == HAL_OK)
        {
          Serial_PutString((uint8_t *)"Write Protection enabled...\r\n");
          Serial_PutString((uint8_t *)"System will now restart...\r\n");
          HAL_FLASH_OB_Launch();
        }
        else
        {
          Serial_PutString((uint8_t *)"Error: Flash write protection failed...\r\n");
        }
      }
      break;
    case '6':
      DecryptAndDownloadMenu();
      break;
    case '7':
      DecryptAESFile();
      break;
    case '8':
      HPatchUpgradeMenu();
      break;
    case '9':
      UART_Passthrough();
      break;
    case 'a':
    case 'A':
      ESP8266_TestMenu();
      break;
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 1-9 or a\r");
      break;
    }
  }
}

static void DecryptAESFile(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  int decrypt_result;
  char src_path[128];
  char dst_path[128];
  char *dot_pos;
  aes_decrypt_config_t decrypt_config = {
      .key = {0x8f, 0x8b, 0x6e, 0xcb, 0x07, 0x78, 0xa9, 0xb7,
              0x10, 0x97, 0x94, 0xb6, 0xe2, 0x31, 0xcd, 0xf3,
              0xc3, 0x40, 0x2e, 0x1d, 0x4f, 0x2a, 0xa3, 0x14,
              0xe0, 0x56, 0x60, 0x16, 0xac, 0xe4, 0x5c, 0xe0},
      .iv = {0xc1, 0xb7, 0x95, 0x2d, 0x38, 0xe6, 0x0e, 0xbd,
             0x33, 0x30, 0x6d, 0xdf, 0x49, 0x2b, 0x7a, 0x58}};

  Serial_PutString((uint8_t *)"\r\nInitializing TF card...\r\n");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    Serial_PutString((uint8_t *)"Error: SD card mount failed! Error code: ");
    Int2Str((uint8_t *)msg, res);
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
    return;
  }

  Serial_PutString((uint8_t *)"Scanning for .bin.aes files...\r\n\r\n");

  scan_sd_card_files();

  uint8_t aes_file_count = 0;
  for (i = 0; i < file_count; i++)
  {
    size_t len = strlen(file_list[i]);
    if (len >= 8)
    {
      const char *ext = file_list[i] + len - 8;
      if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
      {
        snprintf(msg, sizeof(msg), "  [%d] %s\r\n", aes_file_count + 1, file_list[i]);
        Serial_PutString((uint8_t *)msg);
        aes_file_count++;
      }
    }
  }

  if (aes_file_count == 0)
  {
    Serial_PutString((uint8_t *)"No .bin.aes files found on SD card!\r\n");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  BuildSelectionPrompt(msg, sizeof(msg), aes_file_count, "\r\nPlease select a file to decrypt");
  Serial_PutString((uint8_t *)msg);

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  while (1)
  {
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    if (key == 'a' || key == 'A')
    {
      Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= aes_file_count)
      {
        break;
      }
    }
  }

  uint8_t aes_idx = 0;
  for (i = 0; i < file_count; i++)
  {
    size_t len = strlen(file_list[i]);
    if (len >= 8)
    {
      const char *ext = file_list[i] + len - 8;
      if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
      {
        aes_idx++;
        if (aes_idx == selected)
        {
          selected = i;
          break;
        }
      }
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected]);
  Serial_PutString((uint8_t *)msg);

  snprintf(src_path, sizeof(src_path), "0:/%s", file_list[selected]);

  strncpy(dst_path, src_path, sizeof(dst_path) - 1);
  dst_path[sizeof(dst_path) - 1] = '\0';

  dot_pos = strstr(dst_path, ".bin.aes");
  if (dot_pos == NULL)
  {
    dot_pos = strstr(dst_path, ".BIN.AES");
  }

  if (dot_pos != NULL)
  {
    *dot_pos = '\0';
    strcat(dst_path, ".bin");
  }
  else
  {
    strcat(dst_path, ".decrypted");
  }

  snprintf(msg, sizeof(msg), "Output file: %s\r\n", dst_path);
  Serial_PutString((uint8_t *)msg);

  Serial_PutString((uint8_t *)"\r\nStarting decryption...\r\n");

  decrypt_result = aes_decrypt_file_fatfs(&SDFatFS, src_path, dst_path, &decrypt_config);

  if (decrypt_result > 0)
  {
    Serial_PutString((uint8_t *)"\r\nDecryption completed successfully!\r\n");
    snprintf(msg, sizeof(msg), "Decrypted size: %d bytes\r\n", decrypt_result);
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"Output file saved to SD card.\r\n");
  }
  else
  {
    Serial_PutString((uint8_t *)"\r\nDecryption failed! Error code: ");
    Int2Str((uint8_t *)msg, (uint32_t)(-decrypt_result));
    Serial_PutString((uint8_t *)msg);
    Serial_PutString((uint8_t *)"\r\n");
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void DecryptAndDownloadMenu(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  int decrypt_result;
  char src_path[128];
  int lfs_res;
  lfs_t lfs;
  aes_decrypt_config_t decrypt_config = {
      .key = {0x8f, 0x8b, 0x6e, 0xcb, 0x07, 0x78, 0xa9, 0xb7,
              0x10, 0x97, 0x94, 0xb6, 0xe2, 0x31, 0xcd, 0xf3,
              0xc3, 0x40, 0x2e, 0x1d, 0x4f, 0x2a, 0xa3, 0x14,
              0xe0, 0x56, 0x60, 0x16, 0xac, 0xe4, 0x5c, 0xe0},
      .iv = {0xc1, 0xb7, 0x95, 0x2d, 0x38, 0xe6, 0x0e, 0xbd,
             0x33, 0x30, 0x6d, 0xdf, 0x49, 0x2b, 0x7a, 0x58}};

  Serial_PutString((uint8_t *)"\r\n============== Decrypt and Download Menu =================\r\n\n");
  Serial_PutString((uint8_t *)"  Decrypt from SD card and download to Flash ----------- 1\r\n\n");
  Serial_PutString((uint8_t *)"  Decrypt from SPI Flash and download to Flash --------- 2\r\n\n");
  Serial_PutString((uint8_t *)"  Return to Main Menu ---------------------------------- 0\r\n\n");
  Serial_PutString((uint8_t *)"===========================================================\r\n\n");

  __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

  HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

  switch (key)
  {
  case '0':
    Serial_PutString((uint8_t *)"\r\nReturn to Main Menu...\r\n");
    return;

  case '1':
  {
    Serial_PutString((uint8_t *)"\r\nInitializing TF card...\r\n");

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    if (res != FR_OK)
    {
      Serial_PutString((uint8_t *)"Error: SD card mount failed! Error code: ");
      Int2Str((uint8_t *)msg, res);
      Serial_PutString((uint8_t *)msg);
      Serial_PutString((uint8_t *)"\r\n");
      return;
    }

    Serial_PutString((uint8_t *)"Scanning for .bin.aes files...\r\n\r\n");

    scan_sd_card_files();

    uint8_t aes_file_count = 0;
    for (i = 0; i < file_count; i++)
    {
      size_t len = strlen(file_list[i]);
      if (len >= 8)
      {
        const char *ext = file_list[i] + len - 8;
        if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
        {
          snprintf(msg, sizeof(msg), "  [%d] %s\r\n", aes_file_count + 1, file_list[i]);
          Serial_PutString((uint8_t *)msg);
          aes_file_count++;
        }
      }
    }

    if (aes_file_count == 0)
    {
      Serial_PutString((uint8_t *)"No .bin.aes files found on SD card!\r\n");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }

    BuildSelectionPrompt(msg, sizeof(msg), aes_file_count, "\r\nPlease select a file to decrypt and download");
    Serial_PutString((uint8_t *)msg);

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    while (1)
    {
      HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

      if (key == 'a' || key == 'A')
      {
        Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
        f_mount(NULL, (TCHAR const *)SDPath, 0);
        return;
      }

      if (key >= '1' && key <= '9')
      {
        selected = key - '0';
        if (selected >= 1 && selected <= aes_file_count)
        {
          break;
        }
      }
    }

    uint8_t aes_idx = 0;
    for (i = 0; i < file_count; i++)
    {
      size_t len = strlen(file_list[i]);
      if (len >= 8)
      {
        const char *ext = file_list[i] + len - 8;
        if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
        {
          aes_idx++;
          if (aes_idx == selected)
          {
            selected = i;
            break;
          }
        }
      }
    }

    snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected]);
    Serial_PutString((uint8_t *)msg);

    snprintf(src_path, sizeof(src_path), "0:/%s", file_list[selected]);

    Serial_PutString((uint8_t *)"\r\nStarting decryption and download...\r\n");

    decrypt_result = aes_decrypt_to_flash_fatfs(&SDFatFS, src_path, APPLICATION_ADDRESS, &decrypt_config);

    if (decrypt_result > 0)
    {
      Serial_PutString((uint8_t *)"\r\nDecryption and download completed successfully!\r\n");
      snprintf(msg, sizeof(msg), "Written size: %d bytes\r\n", decrypt_result);
      Serial_PutString((uint8_t *)msg);
      Serial_PutString((uint8_t *)"You can now execute the application.\r\n");
    }
    else
    {
      Serial_PutString((uint8_t *)"\r\nDecryption failed! Error code: ");
      Int2Str((uint8_t *)msg, (uint32_t)(-decrypt_result));
      Serial_PutString((uint8_t *)msg);
      Serial_PutString((uint8_t *)"\r\n");
    }

    f_mount(NULL, (TCHAR const *)SDPath, 0);
    break;
  }

  case '2':
  {
    Serial_PutString((uint8_t *)"\r\nInitializing SPI Flash...\r\n");

    lfs_res = lfs_spi_flash_init();
    if (lfs_res != 0)
    {
      Serial_PutString((uint8_t *)"Error: SPI Flash initialization failed!\r\n");
      return;
    }

    Serial_PutString((uint8_t *)"Mounting LittleFS...\r\n");

    lfs_res = lfs_spi_flash_mount(&lfs);
    if (lfs_res != LFS_ERR_OK)
    {
      Serial_PutString((uint8_t *)"Error: LittleFS mount failed!\r\n");
      return;
    }

    Serial_PutString((uint8_t *)"Scanning for .bin.aes files...\r\n\r\n");

    scan_lfs_files(&lfs);

    uint8_t lfs_aes_file_count = 0;
    for (i = 0; i < file_count; i++)
    {
      size_t len = strlen(file_list[i]);
      if (len >= 8)
      {
        const char *ext = file_list[i] + len - 8;
        if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
        {
          snprintf(msg, sizeof(msg), "  [%d] %s\r\n", lfs_aes_file_count + 1, file_list[i]);
          Serial_PutString((uint8_t *)msg);
          lfs_aes_file_count++;
        }
      }
    }

    if (lfs_aes_file_count == 0)
    {
      Serial_PutString((uint8_t *)"No .bin.aes files found on SPI Flash!\r\n");
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    BuildSelectionPrompt(msg, sizeof(msg), lfs_aes_file_count, "\r\nPlease select a file to decrypt and download");
    Serial_PutString((uint8_t *)msg);

    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    while (1)
    {
      HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

      if (key == 'a' || key == 'A')
      {
        Serial_PutString((uint8_t *)"\r\nAborted by user.\r\n");
        lfs_spi_flash_unmount(&lfs);
        return;
      }

      if (key >= '1' && key <= '9')
      {
        selected = key - '0';
        if (selected >= 1 && selected <= lfs_aes_file_count)
        {
          break;
        }
      }
    }

    uint8_t lfs_aes_idx = 0;
    for (i = 0; i < file_count; i++)
    {
      size_t len = strlen(file_list[i]);
      if (len >= 8)
      {
        const char *ext = file_list[i] + len - 8;
        if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
        {
          lfs_aes_idx++;
          if (lfs_aes_idx == selected)
          {
            selected = i;
            break;
          }
        }
      }
    }

    snprintf(msg, sizeof(msg), "\r\nSelected: %s\r\n", file_list[selected]);
    Serial_PutString((uint8_t *)msg);

    Serial_PutString((uint8_t *)"\r\nStarting decryption and download...\r\n");

    decrypt_result = aes_decrypt_to_flash_lfs(&lfs, file_list[selected], APPLICATION_ADDRESS, &decrypt_config);

    if (decrypt_result > 0)
    {
      Serial_PutString((uint8_t *)"\r\nDecryption and download completed successfully!\r\n");
      snprintf(msg, sizeof(msg), "Written size: %d bytes\r\n", decrypt_result);
      Serial_PutString((uint8_t *)msg);
      Serial_PutString((uint8_t *)"You can now execute the application.\r\n");
    }
    else
    {
      Serial_PutString((uint8_t *)"\r\nDecryption failed! Error code: ");
      Int2Str((uint8_t *)msg, (uint32_t)(-decrypt_result));
      Serial_PutString((uint8_t *)msg);
      Serial_PutString((uint8_t *)"\r\n");
    }

    lfs_spi_flash_unmount(&lfs);
    break;
  }

  default:
    Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 0, 1 or 2\r");
    break;
  }
}

/**
 * @}
 */
