/**
 ******************************************************************************
 * @file    IAP/IAP_Main/Src/menu.c
 * @author  MCD Application Team
 * @brief   This file provides the software which contains the main menu routine.
 ******************************************************************************
 */

#include "main.h"
#include "menu_service.h"
#include "platform_uart_stm32_impl.h"
#include "platform_filesystem_fatfs_impl.h"
#include "platform_filesystem_lfs_impl.h"
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
#include "hpatch_service.h"
#include "usart.h"
#include "esp8266_driver.h"
#include "esp8266_ota_api.h"
#include "onenet_ota.h"
#include "esp8266_ota_config.h"
#include "rtc.h"
#include "esp8266_mqtt.h"

#define MAX_FILES 20
#define MAX_FILENAME_LEN 128
#define RX_TIMEOUT ((uint32_t)0xFFFFFFFF)

static menu_ctx_t g_menu_ctx;

uint32_t FlashProtection = 0;
uint8_t aFileName[FILE_NAME_LENGTH];

static char file_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t file_count = 0;

static char hdiff_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t hdiff_count = 0;

static uint8_t check_file_extension(const char *filename)
{
  const char *ext = NULL;
  size_t len = strlen(filename);

  if (len < 4)
    return 0;

  ext = filename + len - 4;
  if (strcmp(ext, ".bin") == 0 || strcmp(ext, ".BIN") == 0)
    return 1;

  if (len >= 6)
  {
    ext = filename + len - 6;
    if (strcmp(ext, ".hdiff") == 0 || strcmp(ext, ".HDIFF") == 0)
      return 2;
  }

  if (len >= 8)
  {
    ext = filename + len - 8;
    if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
      return 1;
  }

  return 0;
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
    menu_service_println(&g_menu_ctx, "Error: Cannot open SD card directory!");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
      break;
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
    menu_service_println(&g_menu_ctx, "Error: Cannot open SD card directory!");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
      break;
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
    menu_service_println(&g_menu_ctx, "Error: Cannot open SPI Flash directory!");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
      break;
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
    menu_service_println(&g_menu_ctx, "Error: Cannot open SPI Flash directory!");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
      break;
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

static void scan_sd_card_hdiff_files(void)
{
  DIR dir;
  FILINFO fno;
  FRESULT res;

  hdiff_count = 0;
  res = f_opendir(&dir, "0:/");
  if (res != FR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot open SD card directory!");
    return;
  }

  while (hdiff_count < MAX_FILES)
  {
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] == 0)
      break;
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

static void scan_lfs_hdiff_files(lfs_t *lfs)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;

  hdiff_count = 0;
  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot open SPI Flash directory!");
    return;
  }

  while (hdiff_count < MAX_FILES)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
      break;
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

static void cmd_serial_download(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t number[11] = {0};
  uint32_t size = 0;
  COM_StatusTypeDef result;

  menu_service_println(ctx, "Waiting for the file to be sent ... (press 'a' to abort)");
  result = Ymodem_Receive(&size);
  if (result == COM_OK)
  {
    HAL_Delay(100);
    menu_service_println(ctx, "\n\nProgramming Completed Successfully!");
    menu_service_println(ctx, "--------------------------------");
    menu_service_printf(ctx, "Name: %s\n", (const char *)aFileName);
    menu_service_int2str(number, size);
    menu_service_printf(ctx, "Size: %s Bytes\n", (const char *)number);
    menu_service_println(ctx, "-------------------");
  }
  else if (result == COM_LIMIT)
  {
    menu_service_println(ctx, "\n\nThe image size is higher than the allowed space memory!");
  }
  else if (result == COM_DATA)
  {
    menu_service_println(ctx, "\n\nVerification failed!");
  }
  else if (result == COM_ABORT)
  {
    menu_service_println(ctx, "\n\nAborted by user.");
  }
  else
  {
    menu_service_println(ctx, "\nFailed to receive the file!");
  }
}

static void cmd_sdcard_download(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  FRESULT res;
  bootloader_err_t err;

  menu_service_println(ctx, "Initializing TF card...");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for bin and aes files...\r");
  scan_sd_card_files();

  if (file_count == 0)
  {
    menu_service_println(ctx, "No bin or aes files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(ctx, "Found bin and aes files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect file (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s", file_list[selected - 1]);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting firmware update...");

  g_fatfs_transport.fs = &SDFatFS;
  strncpy(bootloader_ctx.config.storage.fatfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.fatfs_path) - 1);
  bootloader_ctx.config.storage.fatfs_path[sizeof(bootloader_ctx.config.storage.fatfs_path) - 1] = '\0';
  bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;

  err = bootloader_download(&g_fatfs_transport.base, &g_internal_flash.base, bootloader_ctx.config.storage.fatfs_path);

  if (err == BOOTLOADER_OK)
  {
    menu_service_println(ctx, "Firmware update completed successfully!");
    menu_service_println(ctx, "You can now execute the application.");
  }
  else
  {
    menu_service_print(ctx, "Firmware update failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-err));
    menu_service_println(ctx, msg);
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_spi_flash_download(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  int res;
  bootloader_err_t err;
  lfs_t lfs;

  menu_service_println(ctx, "Initializing SPI Flash...");

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash initialization failed!");
    return;
  }

  menu_service_println(ctx, "Mounting LittleFS...");
  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_print(ctx, "Error: LittleFS mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-res));
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for bin and aes files...\r");
  scan_lfs_files(&lfs);

  if (file_count == 0)
  {
    menu_service_println(ctx, "No bin or aes files found on SPI Flash!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_println(ctx, "Found bin and aes files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect file (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      lfs_spi_flash_unmount(&lfs);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\r\nSelected: %s", file_list[selected - 1]);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting firmware update...");

  g_lfs_transport.lfs = &lfs;
  strncpy(bootloader_ctx.config.storage.lfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.lfs_path) - 1);
  bootloader_ctx.config.storage.lfs_path[sizeof(bootloader_ctx.config.storage.lfs_path) - 1] = '\0';
  bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;

  err = bootloader_download(&g_lfs_transport.base, &g_internal_flash.base, bootloader_ctx.config.storage.lfs_path);

  if (err == BOOTLOADER_OK)
  {
    menu_service_println(ctx, "Firmware update completed successfully!");
    menu_service_println(ctx, "You can now execute the application.");
  }
  else
  {
    menu_service_print(ctx, "Firmware update failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-err));
    menu_service_println(ctx, msg);
  }

  lfs_spi_flash_unmount(&lfs);
}

static void cmd_serial_upload(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t status = 0;

  menu_service_println(ctx, "\n\nSelect Receive File\n");
  menu_service_getchar(ctx, &status, RX_TIMEOUT);
  if (status == CRC16)
  {
    status = Ymodem_Transmit((uint8_t *)APPLICATION_ADDRESS, (const uint8_t *)"UploadedFlashImage.bin", USER_FLASH_SIZE);
    if (status != 0)
    {
      menu_service_println(ctx, "\nError Occurred while Transmitting File\n");
    }
    else
    {
      menu_service_println(ctx, "\nFile uploaded successfully \n");
    }
  }
}

static void cmd_execute_app(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Start program execution......\n");
  bootloader_ctx.config.jump.jump_func(bootloader_ctx.config.jump.app_jump_addr);
}

static void cmd_flash_protection(menu_ctx_t *ctx, int argc, char *argv[])
{
  FlashProtection = FLASH_If_GetWriteProtectionStatus();

  if (FlashProtection != FLASHIF_PROTECTION_NONE)
  {
    if (FLASH_If_WriteProtectionConfig(OB_WRPSTATE_DISABLE) == HAL_OK)
    {
      menu_service_println(ctx, "Write Protection disabled...");
      menu_service_println(ctx, "System will now restart...");
      HAL_FLASH_OB_Launch();
      HAL_FLASH_Unlock();
    }
    else
    {
      menu_service_println(ctx, "Error: Flash write un-protection failed...");
    }
  }
  else
  {
    if (FLASH_If_WriteProtectionConfig(OB_WRPSTATE_ENABLE) == HAL_OK)
    {
      menu_service_println(ctx, "Write Protection enabled...");
      menu_service_println(ctx, "System will now restart...");
      HAL_FLASH_OB_Launch();
    }
    else
    {
      menu_service_println(ctx, "Error: Flash write protection failed...");
    }
  }
}

static void cmd_uart_passthrough(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t rx_data;
  uint8_t q_count = 0;
  uint32_t q_timer = 0;

  menu_service_println(ctx, "\r\n========== UART4 <-> USART1 Passthrough Mode ==========");
  menu_service_println(ctx, "  UART4 (PA0/PA1) <--> USART1 (PA9/PA10)");
  menu_service_println(ctx, "  Baud Rate: 115200, 8N1");
  menu_service_println(ctx, "  For ESP8266/BLE AT Command Debug");
  menu_service_println(ctx, "  Press 'q' 3 times within 1 second to exit");
  menu_service_println(ctx, "========================================================\n");

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
            menu_service_println(ctx, "\n\nExiting passthrough mode...");
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

static void ShowStoredImages(lfs_t *lfs)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int res;
  uint8_t count = 0;
  char msg[128];

  menu_service_println(&g_menu_ctx, "Stored images in SPI Flash:");
  menu_service_println(&g_menu_ctx, "========================================");

  res = lfs_dir_open(lfs, &dir, "/");
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot open directory!");
    return;
  }

  while (1)
  {
    res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0)
      break;
    if (info.type == LFS_TYPE_REG)
    {
      count++;
      snprintf(msg, sizeof(msg), "  [%d] %s  Size: %u bytes", count, info.name, (unsigned int)info.size);
      menu_service_println(&g_menu_ctx, msg);
    }
  }
  lfs_dir_close(lfs, &dir);

  if (count == 0)
    menu_service_println(&g_menu_ctx, "  No images stored.");
  menu_service_println(&g_menu_ctx, "========================================");
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
    menu_service_println(&g_menu_ctx, "No images found to delete!");
    return;
  }

  menu_service_println(&g_menu_ctx, "Select image to delete:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(&g_menu_ctx, msg);
  }

  menu_service_printf(&g_menu_ctx, "\r\nEnter selection (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(&g_menu_ctx);

  while (1)
  {
    menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(&g_menu_ctx, "\rAborted by user.");
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rDeleting: %s", file_list[selected - 1]);
  menu_service_println(&g_menu_ctx, msg);

  res = lfs_remove(lfs, file_list[selected - 1]);
  if (res == LFS_ERR_OK)
    menu_service_println(&g_menu_ctx, "Image deleted successfully!");
  else
    menu_service_println(&g_menu_ctx, "Error: Failed to delete image!");
}

static void DeleteEntireFS(lfs_t *lfs)
{
  uint8_t key = 0;
  int res;

  menu_service_println(&g_menu_ctx, "WARNING: This will delete ALL files in SPI Flash!");
  menu_service_print(&g_menu_ctx, "Press 'y' to confirm, any other key to abort: ");
  menu_service_flush(&g_menu_ctx);
  menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);

  if (key != 'y' && key != 'Y')
  {
    menu_service_println(&g_menu_ctx, "\rAborted by user.");
    return;
  }

  menu_service_println(&g_menu_ctx, "\rFormatting SPI Flash file system...");

  res = lfs_unmount(lfs);
  if (res != LFS_ERR_OK)
    menu_service_println(&g_menu_ctx, "Warning: Unmount failed, continuing...");

  res = lfs_format(lfs, &lfs_spi_flash_cfg);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Format failed!");
    return;
  }

  res = lfs_mount(lfs, &lfs_spi_flash_cfg);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Remount failed!");
    return;
  }

  menu_service_println(&g_menu_ctx, "File system formatted successfully!");
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

  menu_service_println(&g_menu_ctx, "Initializing TF card...");

  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(&g_menu_ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(&g_menu_ctx, msg);
    return;
  }

  menu_service_println(&g_menu_ctx, "Initializing SPI Flash...");
  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(&g_menu_ctx, "Error: SPI Flash initialization failed!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(&g_menu_ctx, "Mounting LittleFS...");
  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: LittleFS mount failed!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(&g_menu_ctx, "Scanning TF card for bin, aes and hdiff files...\r");
  scan_sd_card_files();

  if (file_count == 0)
  {
    menu_service_println(&g_menu_ctx, "No bin, aes or hdiff files found on TF card!");
    lfs_spi_flash_unmount(&lfs);
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(&g_menu_ctx, "Found files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(&g_menu_ctx, msg);
  }

  menu_service_printf(&g_menu_ctx, "\r\nSelect file to store (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(&g_menu_ctx);

  while (1)
  {
    menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(&g_menu_ctx, "\rAborted by user.");
      lfs_spi_flash_unmount(&lfs);
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected - 1]);
  menu_service_println(&g_menu_ctx, msg);
  menu_service_println(&g_menu_ctx, "Storing image to SPI Flash...");

  g_fatfs_transport.fs = &SDFatFS;
  g_lfs_transport.lfs = &lfs;
  strncpy(bootloader_ctx.config.storage.fatfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.fatfs_path) - 1);
  bootloader_ctx.config.storage.fatfs_path[sizeof(bootloader_ctx.config.storage.fatfs_path) - 1] = '\0';
  strncpy(bootloader_ctx.config.storage.lfs_path, file_list[selected - 1], sizeof(bootloader_ctx.config.storage.lfs_path) - 1);
  bootloader_ctx.config.storage.lfs_path[sizeof(bootloader_ctx.config.storage.lfs_path) - 1] = '\0';

  err = bootloader_download(&g_fatfs_transport.base, &g_lfs_transport.base, bootloader_ctx.config.storage.lfs_path);

  if (err == BOOTLOADER_OK)
    menu_service_println(&g_menu_ctx, "Image stored successfully!");
  else
  {
    menu_service_print(&g_menu_ctx, "Store failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-err));
    menu_service_println(&g_menu_ctx, msg);
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

  menu_service_println(&g_menu_ctx, "Initializing SPI Flash...");
  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(&g_menu_ctx, "Error: SPI Flash initialization failed!");
    return;
  }

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: LittleFS mount failed!");
    return;
  }

  menu_service_print(&g_menu_ctx, "\r\nEnter filename (with .bin or .bin.aes extension): ");
  idx = 0;
  menu_service_flush(&g_menu_ctx);

  while (1)
  {
    menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);
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
        menu_service_print(&g_menu_ctx, "\b \b");
      }
    }
    else if (idx < sizeof(filename) - 1 && key >= 0x20 && key <= 0x7E)
    {
      filename[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(&g_menu_ctx, echo_ch);
    }
  }

  if (idx == 0)
  {
    menu_service_println(&g_menu_ctx, "\rError: Empty filename!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  if (!check_file_extension(filename))
  {
    menu_service_println(&g_menu_ctx, "\rError: Invalid file extension! Must be .bin or .bin.aes");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_print(&g_menu_ctx, "\r\nEnter size in bytes (or 'm' for max): ");
  idx = 0;
  menu_service_flush(&g_menu_ctx);

  while (1)
  {
    menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);
    if (key == '\r' || key == '\n')
    {
      size_str[idx] = '\0';
      break;
    }
    else if (idx < sizeof(size_str) - 1)
    {
      if ((key >= '0' && key <= '9') || (idx == 0 && (key == 'm' || key == 'M')))
      {
        size_str[idx++] = key;
        char echo_ch[2] = {(char)key, '\0'};
        menu_service_print(&g_menu_ctx, echo_ch);
      }
    }
  }

  if (idx == 0)
  {
    menu_service_println(&g_menu_ctx, "\rError: Empty size!");
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
        size = size * 10 + (size_str[idx] - '0');
    }
  }

  if (size == 0 || size > USER_FLASH_SIZE)
  {
    menu_service_println(&g_menu_ctx, "\rError: Invalid size!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  snprintf(msg, sizeof(msg), "\rFilename: %s, Size: %u bytes", filename, (unsigned int)size);
  menu_service_println(&g_menu_ctx, msg);
  menu_service_print(&g_menu_ctx, "Confirm? (y/n): ");
  menu_service_flush(&g_menu_ctx);
  menu_service_getchar(&g_menu_ctx, &key, RX_TIMEOUT);

  if (key != 'y' && key != 'Y')
  {
    menu_service_println(&g_menu_ctx, "\rAborted by user.");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_println(&g_menu_ctx, "\rStoring from Internal Flash to SPI Flash...");

  res = lfs_file_open(&lfs, &file, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot create file!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  flash_addr = APPLICATION_ADDRESS;
  flash_size = size;

  while (total_written < flash_size)
  {
    uint32_t to_write = sizeof(buffer);
    if (flash_size - total_written < to_write)
      to_write = flash_size - total_written;

    memcpy(buffer, (uint8_t *)(flash_addr + total_written), to_write);
    bytes_written = lfs_file_write(&lfs, &file, buffer, to_write);
    if (bytes_written != to_write)
    {
      menu_service_println(&g_menu_ctx, "Error: Write failed!");
      lfs_file_close(&lfs, &file);
      lfs_spi_flash_unmount(&lfs);
      return;
    }

    total_written += to_write;
    if ((total_written % 4096) == 0 || total_written == flash_size)
    {
      snprintf(msg, sizeof(msg), "Progress: %u / %u bytes\r", (unsigned int)total_written, (unsigned int)flash_size);
      menu_service_print(&g_menu_ctx, msg);
    }
  }

  lfs_file_close(&lfs, &file);
  lfs_spi_flash_unmount(&lfs);
  menu_service_println(&g_menu_ctx, "\rImage stored successfully!");
}

static void cmd_store_from_tf(menu_ctx_t *ctx, int argc, char *argv[])
{
  StoreFromTFCard();
}

static void cmd_store_from_flash(menu_ctx_t *ctx, int argc, char *argv[])
{
  StoreFromInternalFlash();
}

static void cmd_show_stored(menu_ctx_t *ctx, int argc, char *argv[])
{
  lfs_t lfs;
  int res;

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash init failed!");
    return;
  }

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: Cannot mount SPI Flash!");
    return;
  }

  ShowStoredImages(&lfs);
  lfs_spi_flash_unmount(&lfs);
}

static void cmd_delete_stored(menu_ctx_t *ctx, int argc, char *argv[])
{
  lfs_t lfs;
  int res;

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash init failed!");
    return;
  }

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: Cannot mount SPI Flash!");
    return;
  }

  DeleteStoredImage(&lfs);
  lfs_spi_flash_unmount(&lfs);
}

static void cmd_delete_fs(menu_ctx_t *ctx, int argc, char *argv[])
{
  lfs_t lfs;
  int res;

  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash init failed!");
    return;
  }

  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: Cannot mount SPI Flash!");
    return;
  }

  DeleteEntireFS(&lfs);
  lfs_spi_flash_unmount(&lfs);
}

static void Print_Current_Time(void)
{
  RTC_TimeTypeDef sTime;
  RTC_DateTypeDef sDate;
  char msg[128];

  if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    menu_service_println(&g_menu_ctx, "Failed to get RTC time!");
    return;
  }
  HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

  if (sDate.Year < 25)
  {
    menu_service_println(&g_menu_ctx, "RTC time not set (year < 2025)");
    return;
  }

  uint16_t year = sDate.Year + 2000;
  snprintf(msg, sizeof(msg), "\rCurrent Time: %04d-%02d-%02d %02d:%02d:%02d (UTC+8)",
           year, sDate.Month, sDate.Date, sTime.Hours, sTime.Minutes, sTime.Seconds);
  menu_service_println(&g_menu_ctx, msg);

  uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
  static const uint16_t days_before_month[2][12] = {
      {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
      {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};

  uint32_t days = 0;
  for (uint16_t y = 1970; y < year; y++)
    days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
  days += days_before_month[is_leap][sDate.Month - 1];
  days += sDate.Date - 1;

  uint32_t timestamp = days * 86400UL + sTime.Hours * 3600UL + sTime.Minutes * 60UL + sTime.Seconds;
  snprintf(msg, sizeof(msg), "Unix Timestamp: %lu", (unsigned long)timestamp);
  menu_service_println(&g_menu_ctx, msg);
}

static void cmd_esp8266_init(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Initializing ESP8266 and connecting to AP...");
  esp8266_ota_init();
}

static void cmd_esp8266_at_test(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Testing AT command...");
  if (esp8266_at_test() == ESP8266_EOK)
    menu_service_println(ctx, "AT test OK!");
  else
    menu_service_println(ctx, "AT test FAILED!");
}

static void cmd_esp8266_tcp_connect(menu_ctx_t *ctx, int argc, char *argv[])
{
  char server_ip[64];
  char server_port[8];
  char msg[128];
  uint8_t key = 0;
  uint8_t idx = 0;

  menu_service_print(ctx, "\r\nEnter server IP: ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(server_ip) - 1 && key >= 0x20 && key <= 0x7E)
    {
      server_ip[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }

  menu_service_print(ctx, "\r\nEnter server port: ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(server_port) - 1 && key >= '0' && key <= '9')
    {
      server_port[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }

  snprintf(msg, sizeof(msg), "\r\nConnecting to %s:%s...", server_ip, server_port);
  menu_service_println(ctx, msg);

  if (esp8266_connect_tcp_server(server_ip, server_port) == ESP8266_EOK)
    menu_service_println(ctx, "TCP connect OK!");
  else
    menu_service_println(ctx, "TCP connect FAILED!");
}

static void cmd_esp8266_transparent_enter(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Entering transparent mode...");
  if (esp8266_enter_unvarnished() == ESP8266_EOK)
  {
    menu_service_println(ctx, "Transparent mode enabled!");
    menu_service_println(ctx, "You can now send data directly.");
  }
  else
    menu_service_println(ctx, "Enter transparent mode FAILED!");
}

static void cmd_esp8266_transparent_exit(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Exiting transparent mode...");
  esp8266_exit_unvarnished();
  menu_service_println(ctx, "Transparent mode exited.");
}

static void cmd_ota_target_internal(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to Internal Flash...");
  ONENET_OTA_SetTargetType(0);
}

static void cmd_ota_target_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to SD Card (FATFS)...");
  ONENET_OTA_SetTargetType(1);
}

static void cmd_ota_target_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to SPI Flash (LFS)...");
  ONENET_OTA_SetTargetType(2);
}

static void cmd_onenet_ota_download(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Starting OneNET OTA download...");
  ONENET_OTA_ProcessUpgrade();
}

static void cmd_show_time(menu_ctx_t *ctx, int argc, char *argv[])
{
  Print_Current_Time();
}

static void cmd_mqtt_check_status(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char mqtt_cmd[256];

  menu_service_println(ctx, "Checking MQTT connection status...");
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
              menu_service_println(ctx, "MQTT: Not connected (server is empty)");
            else
            {
              menu_service_println(ctx, "MQTT: Connected");
              menu_service_println(ctx, conn_line);
            }
          }
          else
            menu_service_println(ctx, "MQTT: Not connected");
        }
        break;
      }
      else if (strstr((const char *)resp, "ERROR") != NULL)
      {
        menu_service_println(ctx, "MQTT: Not configured");
        break;
      }
      esp8266_uart_rx_restart();
    }
    timeout--;
    HAL_Delay(1);
  }
  if (timeout == 0)
    menu_service_println(ctx, "MQTT: Check timeout");
}

static void cmd_mqtt_configure(menu_ctx_t *ctx, int argc, char *argv[])
{
  mqtt_user_config_t config;
  char msg[256];
  uint8_t key = 0;
  uint8_t idx = 0;
  char input_buf[256];

  memset(&config, 0, sizeof(config));
  strncpy(config.client_id, ONENET_DEVICE_NAME, sizeof(config.client_id) - 1);
  strncpy(config.username, ONENET_PRODUCT_ID, sizeof(config.username) - 1);

  menu_service_println(ctx, "\rPress '!' to use macro defaults, or Enter to customize...");
  snprintf(msg, sizeof(msg), "Default: client=%s, user=%s", config.client_id, config.username);
  menu_service_println(ctx, msg);

  menu_service_print(ctx, "\r\nEnter Client ID (or Enter for default): ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == '\r' || key == '\n')
    {
      input_buf[idx] = '\0';
      break;
    }
    else if (key == '!' && idx == 0)
    {
      input_buf[0] = '!';
      input_buf[1] = '\0';
      menu_service_print(ctx, "!");
      break;
    }
    else if (key == 0x08 || key == 0x7F)
    {
      if (idx > 0)
      {
        idx--;
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
    {
      input_buf[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }

  if (strcmp(input_buf, "!") == 0)
  {
    menu_service_println(ctx, "\rUsing macro defaults...");
    strncpy(config.client_id, ONENET_DEVICE_NAME, sizeof(config.client_id) - 1);
    strncpy(config.username, ONENET_PRODUCT_ID, sizeof(config.username) - 1);
    strncpy(config.password, ONENET_MQTT_TOKEN, sizeof(config.password) - 1);
  }
  else
  {
    if (idx > 0)
      strncpy(config.client_id, input_buf, sizeof(config.client_id) - 1);

    menu_service_print(ctx, "\r\nEnter Username (Product ID): ");
    idx = 0;
    menu_service_flush(ctx);
    while (1)
    {
      menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
          menu_service_print(ctx, "\b \b");
        }
      }
      else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
      {
        input_buf[idx++] = key;
        char echo_ch[2] = {(char)key, '\0'};
        menu_service_print(ctx, echo_ch);
      }
    }
    if (idx > 0)
      strncpy(config.username, input_buf, sizeof(config.username) - 1);

    menu_service_print(ctx, "\r\nEnter Password (Token): ");
    idx = 0;
    menu_service_flush(ctx);
    while (1)
    {
      menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
          menu_service_print(ctx, "\b \b");
        }
      }
      else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
      {
        input_buf[idx++] = key;
        char echo_ch[2] = {(char)key, '\0'};
        menu_service_print(ctx, echo_ch);
      }
    }
    if (idx > 0)
      strncpy(config.password, input_buf, sizeof(config.password) - 1);
  }

  snprintf(msg, sizeof(msg), "\rConfiguring MQTT user: client=%s, user=%s", config.client_id, config.username);
  menu_service_println(ctx, msg);

  if (esp8266_mqtt_usercfg(0, &config) == ESP8266_EOK)
    menu_service_println(ctx, "MQTT user config OK!");
  else
    menu_service_println(ctx, "MQTT user config FAILED!");
}

static void cmd_mqtt_connect(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char mqtt_cmd[256];
  char host[64] = ONENET_MQTT_HOST;
  uint16_t port = ONENET_MQTT_PORT;
  uint8_t key = 0;
  uint8_t idx = 0;
  char input_buf[128];

  menu_service_println(ctx, "Checking if already connected...");
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
              menu_service_println(ctx, "MQTT: Already connected!");
              menu_service_println(ctx, conn_line);
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
    return;

  menu_service_println(ctx, "Connecting to MQTT server...");
  snprintf(msg, sizeof(msg), "\r\nEnter MQTT server host (default: %s): ", host);
  menu_service_print(ctx, msg);
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(input_buf) - 1 && key >= 0x20 && key <= 0x7E)
    {
      input_buf[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }
  if (idx > 0)
    strncpy(host, input_buf, sizeof(host) - 1);

  if (esp8266_mqtt_connect(0, host, port, 1) == ESP8266_EOK)
    menu_service_println(ctx, "MQTT connect OK!");
  else
    menu_service_println(ctx, "MQTT connect FAILED!");
}

static void cmd_mqtt_subscribe(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char topic[128];

  menu_service_println(ctx, "Subscribing to property topics...");

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/get/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/delete/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/get", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  esp8266_mqtt_subscribe(0, topic, 0);
}

static void cmd_mqtt_publish(menu_ctx_t *ctx, int argc, char *argv[])
{
  mqtt_property_t prop;
  char msg[256];
  char msg_id[32] = "007";
  char type_buf[4];
  char value_buf[64];
  uint8_t key = 0;
  uint8_t idx = 0;

  memset(&prop, 0, sizeof(prop));

  menu_service_print(ctx, "\r\nEnter message ID (default: 007): ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(msg_id) - 1 && key >= 0x20 && key <= 0x7E)
    {
      msg_id[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }
  if (idx == 0)
    strcpy(msg_id, "007");

  menu_service_print(ctx, "\r\nEnter property key (e.g. BSP_LED): ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(prop.key) - 1 && key >= 0x20 && key <= 0x7E)
    {
      prop.key[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }

  menu_service_print(ctx, "\r\nEnter value type (0=int, 1=float, 2=bool, 3=string): ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == '\r' || key == '\n')
    {
      type_buf[idx] = '\0';
      break;
    }
    else if (key >= '0' && key <= '9' && idx < sizeof(type_buf) - 1)
    {
      type_buf[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }
  prop.value_type = (uint8_t)atoi(type_buf);

  menu_service_print(ctx, "\r\nEnter value: ");
  idx = 0;
  menu_service_flush(ctx);
  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
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
        menu_service_print(ctx, "\b \b");
      }
    }
    else if (idx < sizeof(value_buf) - 1 && key >= 0x20 && key <= 0x7E)
    {
      value_buf[idx++] = key;
      char echo_ch[2] = {(char)key, '\0'};
      menu_service_print(ctx, echo_ch);
    }
  }

  if (prop.value_type == MQTT_VALUE_TYPE_FLOAT)
    prop.value_float = atof(value_buf);
  else if (prop.value_type == MQTT_VALUE_TYPE_BOOL)
    prop.value_int = (strcmp(value_buf, "true") == 0 || strcmp(value_buf, "1") == 0) ? 1 : 0;
  else if (prop.value_type == MQTT_VALUE_TYPE_STRING)
    strncpy(prop.id, value_buf, sizeof(prop.id) - 1);
  else
    prop.value_int = atoi(value_buf);

  snprintf(msg, sizeof(msg), "\rPublishing: %s = %s (type=%d, id=%s)", prop.key, value_buf, prop.value_type, msg_id);
  menu_service_println(ctx, msg);

  if (esp8266_mqtt_publish_property(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, &prop, 1, msg_id) == ESP8266_EOK)
    menu_service_println(ctx, "Publish OK!");
  else
    menu_service_println(ctx, "Publish FAILED!");
}

static void cmd_mqtt_listen(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char recv_topic[128];
  char recv_payload[512];
  char recv_msg_id[32];
  uint8_t exit_q_count = 0;
  uint32_t exit_q_timer = 0;

  menu_service_println(ctx, "Listening for property set commands...");
  menu_service_println(ctx, "Press 'q' 3 times within 1 second to exit");

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
            menu_service_println(ctx, "Exiting listen mode...");
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
        exit_q_count = 0;
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
          snprintf(msg, sizeof(msg), "\rReceived: topic=%s", recv_topic);
          menu_service_println(ctx, msg);
          snprintf(msg, sizeof(msg), "Payload: %s", recv_payload);
          menu_service_println(ctx, msg);
          snprintf(msg, sizeof(msg), "Message ID: %s", recv_msg_id);
          menu_service_println(ctx, msg);

          if (strstr(recv_topic, "/thing/property/set") != NULL)
          {
            menu_service_println(ctx, "Auto replying to property set...");
            if (esp8266_mqtt_publish_set_reply(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, recv_msg_id, 200, "user_succ") == ESP8266_EOK)
              menu_service_println(ctx, "Reply sent successfully!");
            else
              menu_service_println(ctx, "Reply FAILED!");
          }
        }
      }
      esp8266_uart_rx_restart();
    }
    HAL_Delay(10);
  }
}

static void cmd_mqtt_disconnect(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Disconnecting MQTT...");
  if (esp8266_mqtt_disconnect(0) == ESP8266_EOK)
    menu_service_println(ctx, "MQTT disconnected!");
  else
    menu_service_println(ctx, "Disconnect FAILED!");
}

static void cmd_mqtt_sync_time(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Syncing time from server...");
  if (ONENET_SyncTime())
  {
    menu_service_println(ctx, "Time sync success!");
    Print_Current_Time();
  }
  else
    menu_service_println(ctx, "Time sync FAILED!");
}

static void cmd_mqtt_publish_rtc(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char rtc_time_str[64];
  uint32_t publish_count = 0;
  uint8_t key = 0;

  menu_service_println(ctx, "Publishing RTC time every 1s...");
  menu_service_println(ctx, "Press 'q' or 'Q' to exit");

  while (1)
  {
    if (menu_service_getchar(ctx, &key, 10) > 0)
    {
      if (key == 'q' || key == 'Q')
      {
        menu_service_println(ctx, "Exiting RTC time publish mode...");
        snprintf(msg, sizeof(msg), "Total published: %lu times", (unsigned long)publish_count);
        menu_service_println(ctx, msg);
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

      if (esp8266_mqtt_publish_property(0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, &prop, 1, "007") == ESP8266_EOK)
      {
        publish_count++;
        snprintf(msg, sizeof(msg), "Published %lu times: %s", (unsigned long)publish_count, rtc_time_str);
        menu_service_println(ctx, msg);
      }
    }

    HAL_Delay(1000);
  }
}

static void cmd_decrypt_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
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

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for .bin.aes files...\r");
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
        snprintf(msg, sizeof(msg), "  [%d] %s", aes_file_count + 1, file_list[i]);
        menu_service_println(ctx, msg);
        aes_file_count++;
      }
    }
  }

  if (aes_file_count == 0)
  {
    menu_service_println(ctx, "No .bin.aes files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_printf(ctx, "\r\nSelect file to decrypt (1-%d) or 'a' to abort: ", aes_file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= aes_file_count)
        break;
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

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected]);
  menu_service_println(ctx, msg);

  snprintf(src_path, sizeof(src_path), "0:/%s", file_list[selected]);
  strncpy(dst_path, src_path, sizeof(dst_path) - 1);
  dst_path[sizeof(dst_path) - 1] = '\0';

  dot_pos = strstr(dst_path, ".bin.aes");
  if (dot_pos == NULL)
    dot_pos = strstr(dst_path, ".BIN.AES");
  if (dot_pos != NULL)
  {
    *dot_pos = '\0';
    strcat(dst_path, ".bin");
  }
  else
    strcat(dst_path, ".decrypted");

  snprintf(msg, sizeof(msg), "Output file: %s", dst_path);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting decryption...");

  decrypt_result = aes_decrypt_file_fatfs(&SDFatFS, src_path, dst_path, &decrypt_config);

  if (decrypt_result > 0)
  {
    menu_service_println(ctx, "Decryption completed successfully!");
    snprintf(msg, sizeof(msg), "Decrypted size: %d bytes", decrypt_result);
    menu_service_println(ctx, msg);
    menu_service_println(ctx, "Output file saved to SD card.");
  }
  else
  {
    menu_service_print(ctx, "Decryption failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-decrypt_result));
    menu_service_println(ctx, msg);
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_decrypt_download_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  int decrypt_result;
  char src_path[128];
  aes_decrypt_config_t decrypt_config = {
      .key = {0x8f, 0x8b, 0x6e, 0xcb, 0x07, 0x78, 0xa9, 0xb7,
              0x10, 0x97, 0x94, 0xb6, 0xe2, 0x31, 0xcd, 0xf3,
              0xc3, 0x40, 0x2e, 0x1d, 0x4f, 0x2a, 0xa3, 0x14,
              0xe0, 0x56, 0x60, 0x16, 0xac, 0xe4, 0x5c, 0xe0},
      .iv = {0xc1, 0xb7, 0x95, 0x2d, 0x38, 0xe6, 0x0e, 0xbd,
             0x33, 0x30, 0x6d, 0xdf, 0x49, 0x2b, 0x7a, 0x58}};

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for .bin.aes files...\r");
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
        snprintf(msg, sizeof(msg), "  [%d] %s", aes_file_count + 1, file_list[i]);
        menu_service_println(ctx, msg);
        aes_file_count++;
      }
    }
  }

  if (aes_file_count == 0)
  {
    menu_service_println(ctx, "No .bin.aes files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_printf(ctx, "\r\nSelect file to decrypt and download (1-%d) or 'a' to abort: ", aes_file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= aes_file_count)
        break;
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

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected]);
  menu_service_println(ctx, msg);
  snprintf(src_path, sizeof(src_path), "0:/%s", file_list[selected]);

  menu_service_println(ctx, "Starting decryption and download...");

  decrypt_result = aes_decrypt_to_flash_fatfs(&SDFatFS, src_path, APPLICATION_ADDRESS, &decrypt_config);

  if (decrypt_result > 0)
  {
    menu_service_println(ctx, "Decryption and download completed successfully!");
    snprintf(msg, sizeof(msg), "Written size: %d bytes", decrypt_result);
    menu_service_println(ctx, msg);
    menu_service_println(ctx, "You can now execute the application.");
  }
  else
  {
    menu_service_print(ctx, "Decryption failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-decrypt_result));
    menu_service_println(ctx, msg);
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_decrypt_download_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  int res;
  int decrypt_result;
  lfs_t lfs;
  aes_decrypt_config_t decrypt_config = {
      .key = {0x8f, 0x8b, 0x6e, 0xcb, 0x07, 0x78, 0xa9, 0xb7,
              0x10, 0x97, 0x94, 0xb6, 0xe2, 0x31, 0xcd, 0xf3,
              0xc3, 0x40, 0x2e, 0x1d, 0x4f, 0x2a, 0xa3, 0x14,
              0xe0, 0x56, 0x60, 0x16, 0xac, 0xe4, 0x5c, 0xe0},
      .iv = {0xc1, 0xb7, 0x95, 0x2d, 0x38, 0xe6, 0x0e, 0xbd,
             0x33, 0x30, 0x6d, 0xdf, 0x49, 0x2b, 0x7a, 0x58}};

  menu_service_println(ctx, "Initializing SPI Flash...");
  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash initialization failed!");
    return;
  }

  menu_service_println(ctx, "Mounting LittleFS...");
  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: LittleFS mount failed!");
    return;
  }

  menu_service_println(ctx, "Scanning for .bin.aes files...\r");
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
        snprintf(msg, sizeof(msg), "  [%d] %s", lfs_aes_file_count + 1, file_list[i]);
        menu_service_println(ctx, msg);
        lfs_aes_file_count++;
      }
    }
  }

  if (lfs_aes_file_count == 0)
  {
    menu_service_println(ctx, "No .bin.aes files found on SPI Flash!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_printf(ctx, "\r\nSelect file to decrypt and download (1-%d) or 'a' to abort: ", lfs_aes_file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      lfs_spi_flash_unmount(&lfs);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= lfs_aes_file_count)
        break;
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

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected]);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting decryption and download...");

  decrypt_result = aes_decrypt_to_flash_lfs(&lfs, file_list[selected], APPLICATION_ADDRESS, &decrypt_config);

  if (decrypt_result > 0)
  {
    menu_service_println(ctx, "Decryption and download completed successfully!");
    snprintf(msg, sizeof(msg), "Written size: %d bytes", decrypt_result);
    menu_service_println(ctx, msg);
    menu_service_println(ctx, "You can now execute the application.");
  }
  else
  {
    menu_service_print(ctx, "Decryption failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, (uint32_t)(-decrypt_result));
    menu_service_println(ctx, msg);
  }

  lfs_spi_flash_unmount(&lfs);
}

static void cmd_hpatch_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected_diff = 0;
  uint8_t selected_old = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  hpatch_err_t patch_result;
  hpatch_config_t config;
  char diff_path[HPATCH_MAX_PATH_LEN];
  char old_path[HPATCH_MAX_PATH_LEN];
  char out_path[HPATCH_MAX_PATH_LEN];
  char *dot_pos;
  const char *upgrade_tag = "_HdiffUpgraded";

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for .hdiff files...\r");
  scan_sd_card_hdiff_files();

  if (hdiff_count == 0)
  {
    menu_service_println(ctx, "No .hdiff files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(ctx, "Found .hdiff files:");
  for (i = 0; i < hdiff_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, hdiff_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect .hdiff file (1-%d) or 'a' to abort: ", hdiff_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected_diff = key - '0';
      if (selected_diff >= 1 && selected_diff <= hdiff_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", hdiff_list[selected_diff - 1]);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Scanning for .bin firmware files...\r");
  scan_sd_card_files_filter(1);

  if (file_count == 0)
  {
    menu_service_println(ctx, "No .bin files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_println(ctx, "Found firmware files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect old firmware file to update (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      f_mount(NULL, (TCHAR const *)SDPath, 0);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected_old = key - '0';
      if (selected_old >= 1 && selected_old <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected_old - 1]);
  menu_service_println(ctx, msg);

  snprintf(diff_path, sizeof(diff_path), "0:/%s", hdiff_list[selected_diff - 1]);
  snprintf(old_path, sizeof(old_path), "0:/%s", file_list[selected_old - 1]);

  strncpy(out_path, old_path, sizeof(out_path) - 1);
  out_path[sizeof(out_path) - 1] = '\0';

  dot_pos = strrchr(out_path, '.');
  if (dot_pos != NULL)
  {
    size_t base_len = dot_pos - out_path;
    if (base_len + strlen(upgrade_tag) + 4 < sizeof(out_path))
      snprintf(dot_pos, sizeof(out_path) - base_len, "%s.bin", upgrade_tag);
    else
      strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
  }
  else
  {
    strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    strncat(out_path, ".bin", sizeof(out_path) - strlen(out_path) - 1);
  }

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");
  config.fs = &g_fs_fatfs.base;
  config.diff_path = diff_path;
  config.old_path = old_path;
  config.out_path = out_path;

  snprintf(msg, sizeof(msg), "\rDiff file: %s", config.diff_path);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Old firmware: %s", config.old_path);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Output file: %s", config.out_path);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting HPatch differential upgrade...");

  patch_result = hpatch_upgrade(&config);

  if (patch_result == HPATCH_OK)
  {
    menu_service_println(ctx, "HPatch upgrade completed successfully!");
    menu_service_println(ctx, "Upgraded firmware saved to SD card.");
    snprintf(msg, sizeof(msg), "Output: %s", config.out_path);
    menu_service_println(ctx, msg);
  }
  else
  {
    menu_service_print(ctx, "HPatch upgrade failed! Error: ");
    menu_service_println(ctx, hpatch_err_to_string(patch_result));
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_hpatch_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected_diff = 0;
  uint8_t selected_old = 0;
  uint8_t i;
  char msg[256];
  int res;
  lfs_t lfs;
  hpatch_err_t patch_result;
  hpatch_config_t config;
  char out_path[HPATCH_MAX_PATH_LEN];
  char *dot_pos;
  const char *upgrade_tag = "_HdiffUpgraded";

  menu_service_println(ctx, "Initializing SPI Flash...");
  res = lfs_spi_flash_init();
  if (res != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash initialization failed!");
    return;
  }

  menu_service_println(ctx, "Mounting LittleFS...");
  res = lfs_spi_flash_mount(&lfs);
  if (res != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: LittleFS mount failed!");
    return;
  }

  menu_service_println(ctx, "Scanning for .hdiff files...\r");
  scan_lfs_hdiff_files(&lfs);

  if (hdiff_count == 0)
  {
    menu_service_println(ctx, "No .hdiff files found on SPI Flash!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_println(ctx, "Found .hdiff files:");
  for (i = 0; i < hdiff_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, hdiff_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect .hdiff file (1-%d) or 'a' to abort: ", hdiff_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      lfs_spi_flash_unmount(&lfs);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected_diff = key - '0';
      if (selected_diff >= 1 && selected_diff <= hdiff_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", hdiff_list[selected_diff - 1]);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Scanning for .bin firmware files...\r");
  scan_lfs_files_filter(&lfs, 1);

  if (file_count == 0)
  {
    menu_service_println(ctx, "No .bin files found on SPI Flash!");
    lfs_spi_flash_unmount(&lfs);
    return;
  }

  menu_service_println(ctx, "Found firmware files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect old firmware file to update (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      lfs_spi_flash_unmount(&lfs);
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected_old = key - '0';
      if (selected_old >= 1 && selected_old <= file_count)
        break;
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected_old - 1]);
  menu_service_println(ctx, msg);

  strncpy(out_path, file_list[selected_old - 1], sizeof(out_path) - 1);
  out_path[sizeof(out_path) - 1] = '\0';

  dot_pos = strrchr(out_path, '.');
  if (dot_pos != NULL)
  {
    size_t base_len = dot_pos - out_path;
    if (base_len + strlen(upgrade_tag) + 4 < sizeof(out_path))
      snprintf(dot_pos, sizeof(out_path) - base_len, "%s.bin", upgrade_tag);
    else
      strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
  }
  else
  {
    strncat(out_path, upgrade_tag, sizeof(out_path) - strlen(out_path) - 1);
    strncat(out_path, ".bin", sizeof(out_path) - strlen(out_path) - 1);
  }

  platform_fs_lfs_register(&g_fs_lfs, &lfs, "lfs");
  config.fs = &g_fs_lfs.base;
  config.diff_path = hdiff_list[selected_diff - 1];
  config.old_path = file_list[selected_old - 1];
  config.out_path = out_path;

  snprintf(msg, sizeof(msg), "\rDiff file: %s", config.diff_path);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Old firmware: %s", config.old_path);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Output file: %s", config.out_path);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting HPatch differential upgrade...");

  patch_result = hpatch_upgrade(&config);

  if (patch_result == HPATCH_OK)
  {
    menu_service_println(ctx, "HPatch upgrade completed successfully!");
    menu_service_println(ctx, "Upgraded firmware saved to SPI Flash.");
    snprintf(msg, sizeof(msg), "Output: %s", config.out_path);
    menu_service_println(ctx, msg);
  }
  else
  {
    menu_service_print(ctx, "HPatch upgrade failed! Error: ");
    menu_service_println(ctx, hpatch_err_to_string(patch_result));
  }

  lfs_spi_flash_unmount(&lfs);
}

MENU_TABLE(mqtt_menu) = {
    MENU_ITEM_CMD("1", "Check MQTT Connection Status", "Query current MQTT connection", cmd_mqtt_check_status),
    MENU_ITEM_CMD("2", "Configure MQTT User", "Set client ID, username, password", cmd_mqtt_configure),
    MENU_ITEM_CMD("3", "Connect to MQTT Server", "Connect to OneNET MQTT broker", cmd_mqtt_connect),
    MENU_ITEM_CMD("4", "Subscribe Property Topics", "Subscribe to all property topics", cmd_mqtt_subscribe),
    MENU_ITEM_CMD("5", "Publish Property", "Publish a single property value", cmd_mqtt_publish),
    MENU_ITEM_CMD("6", "Listen & Auto Reply Property Set", "Listen for property set commands", cmd_mqtt_listen),
    MENU_ITEM_CMD("7", "Disconnect MQTT", "Disconnect from MQTT broker", cmd_mqtt_disconnect),
    MENU_ITEM_CMD("8", "Sync Time (SNTP)", "Synchronize RTC from NTP server", cmd_mqtt_sync_time),
    MENU_ITEM_CMD("9", "Publish RTC Time (1s interval)", "Publish RTC time periodically", cmd_mqtt_publish_rtc),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(esp8266_menu) = {
    MENU_ITEM_CMD("1", "WiFi Init & Connect AP", "Initialize ESP8266 and connect", cmd_esp8266_init),
    MENU_ITEM_CMD("2", "AT Command Test", "Test ESP8266 AT communication", cmd_esp8266_at_test),
    MENU_ITEM_CMD("3", "TCP Connect Test", "Connect to TCP server", cmd_esp8266_tcp_connect),
    MENU_ITEM_CMD("4", "Enter Transparent Mode", "Enable transparent transmission", cmd_esp8266_transparent_enter),
    MENU_ITEM_CMD("5", "Exit Transparent Mode", "Disable transparent transmission", cmd_esp8266_transparent_exit),
    MENU_ITEM_CMD("6", "Set OTA Target: Internal Flash", "OTA downloads to internal flash", cmd_ota_target_internal),
    MENU_ITEM_CMD("7", "Set OTA Target: SD Card (FATFS)", "OTA downloads to SD card", cmd_ota_target_sdcard),
    MENU_ITEM_CMD("8", "Set OTA Target: SPI Flash (LFS)", "OTA downloads to SPI flash", cmd_ota_target_spi),
    MENU_ITEM_CMD("9", "OneNET OTA Download", "Start OTA download from OneNET", cmd_onenet_ota_download),
    MENU_ITEM_CMD("A", "Show Current Time", "Display RTC time and timestamp", cmd_show_time),
    MENU_ITEM_SUBMENU("B", "MQTT Test Menu", "MQTT protocol testing", mqtt_menu, sizeof(mqtt_menu) / sizeof(mqtt_menu[0]) - 1),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(store_menu) = {
    MENU_ITEM_CMD("1", "Store image from TF card", "Copy from SD to SPI Flash", cmd_store_from_tf),
    MENU_ITEM_CMD("2", "Store image from Flash", "Copy from internal Flash to SPI Flash", cmd_store_from_flash),
    MENU_ITEM_CMD("3", "Show stored images", "List images in SPI Flash", cmd_show_stored),
    MENU_ITEM_CMD("4", "Delete stored image", "Remove image from SPI Flash", cmd_delete_stored),
    MENU_ITEM_CMD("5", "Delete entire file system", "Format SPI Flash", cmd_delete_fs),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(download_menu) = {
    MENU_ITEM_CMD("1", "Download via Serial (Ymodem)", "Receive firmware via Ymodem", cmd_serial_download),
    MENU_ITEM_CMD("2", "Download from SD card (FATFS)", "Read firmware from SD card", cmd_sdcard_download),
    MENU_ITEM_CMD("3", "Download from SPI Flash (LittleFS)", "Read firmware from SPI Flash", cmd_spi_flash_download),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(decrypt_menu) = {
    MENU_ITEM_CMD("1", "Decrypt from SD card and download to Flash", "Decrypt .bin.aes and write to Flash", cmd_decrypt_download_sdcard),
    MENU_ITEM_CMD("2", "Decrypt from SPI Flash and download to Flash", "Decrypt .bin.aes and write to Flash", cmd_decrypt_download_spi),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(hpatch_menu) = {
    MENU_ITEM_CMD("1", "HPatch upgrade from SD card", "Apply .hdiff patch from SD card", cmd_hpatch_sdcard),
    MENU_ITEM_CMD("2", "HPatch upgrade from SPI Flash", "Apply .hdiff patch from SPI Flash", cmd_hpatch_spi),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};

MENU_TABLE(main_menu) = {
    MENU_ITEM_SUBMENU("1", "Download image to internal Flash", "Firmware download options", download_menu, sizeof(download_menu) / sizeof(download_menu[0]) - 1),
    MENU_ITEM_CMD("2", "Upload image from internal Flash", "Send firmware via Ymodem", cmd_serial_upload),
    MENU_ITEM_SUBMENU("3", "Store image to SPI-Flash LFS", "SPI Flash storage management", store_menu, sizeof(store_menu) / sizeof(store_menu[0]) - 1),
    MENU_ITEM_CMD("4", "Execute the loaded application", "Jump to application firmware", cmd_execute_app),
    MENU_ITEM_CMD("5", "Toggle Flash write protection", "Enable/disable write protection", cmd_flash_protection),
    MENU_ITEM_SUBMENU("6", "Decrypt and download encrypted firmware", "AES decryption options", decrypt_menu, sizeof(decrypt_menu) / sizeof(decrypt_menu[0]) - 1),
    MENU_ITEM_CMD("7", "Decrypt .bin.aes file on SD card", "Decrypt and save to SD card", cmd_decrypt_sdcard),
    MENU_ITEM_SUBMENU("8", "HPatch differential upgrade", "Differential firmware update", hpatch_menu, sizeof(hpatch_menu) / sizeof(hpatch_menu[0]) - 1),
    MENU_ITEM_CMD("9", "UART4 <-> USART1 Passthrough", "Transparent UART bridge", cmd_uart_passthrough),
    MENU_ITEM_SUBMENU("A", "ESP8266 WiFi & OTA Test", "WiFi and OTA testing", esp8266_menu, sizeof(esp8266_menu) / sizeof(esp8266_menu[0]) - 1),
    MENU_TABLE_END};

void Main_Menu(void)
{
  menu_service_init(&g_menu_ctx, (platform_uart_base_t *)&g_uart4_console.base);
  menu_service_set_root(&g_menu_ctx, main_menu, sizeof(main_menu) / sizeof(main_menu[0]) - 1);
  menu_service_set_prompt(&g_menu_ctx, "STM32F4xx IAP");

  menu_service_println(&g_menu_ctx, "\r\n======================================================================");
  menu_service_println(&g_menu_ctx, "=              (C) COPYRIGHT 2016 STMicroelectronics                 =");
  menu_service_println(&g_menu_ctx, "=                                                                    =");
  menu_service_println(&g_menu_ctx, "=          STM32F4xx In-Application Programming Application          =");
  menu_service_println(&g_menu_ctx, "=                                                                    =");
  menu_service_println(&g_menu_ctx, "=                       By MCD Application Team                      =");
  menu_service_println(&g_menu_ctx, "======================================================================");
  menu_service_println(&g_menu_ctx, "");

  menu_service_run(&g_menu_ctx);
}

/**
 * @}
 */
