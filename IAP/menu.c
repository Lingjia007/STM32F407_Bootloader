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
#include "aes_decrypt.h"
#include "hpatch_upgrade.h"

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
      snprintf(msg, sizeof(msg), "  [%d] %s  Size: %lu bytes\r\n", count, info.name, (uint32_t)info.size);
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

  Serial_PutString((uint8_t *)"Scanning TF card for bin and aes files...\r\n\r\n");

  scan_sd_card_files();

  if (file_count == 0)
  {
    Serial_PutString((uint8_t *)"No bin or aes files found on TF card!\r\n");
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

  snprintf(msg, sizeof(msg), "\r\nFilename: %s, Size: %lu bytes\r\n", filename, size);
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
      snprintf(msg, sizeof(msg), "Progress: %lu / %lu bytes\r", total_written, flash_size);
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

static void HPatchUpgradeMenu(void)
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

  scan_sd_card_files();

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
    Serial_PutString((uint8_t *)"  HPatch differential upgrade (SD card)  --------------- 8\r\n\n");

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
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be 1, 2, 3, 4, 5, 6, 7 or 8\r");
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
