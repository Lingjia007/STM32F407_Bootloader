/**
 ******************************************************************************
 * @file    IAP/IAP_Main/Src/menu.c
 * @author  MCD Application Team
 * @brief   This file provides the software which contains the main menu routine.
 ******************************************************************************
 */

#include "main.h"
#include "service_menu.h"
#include "platform_config.h"
#include "platform_filesystem_fatfs_impl.h"
#include "platform_filesystem_lfs_impl.h"
#include "menu.h"
#include "ymodem.h"
#include "fatfs.h"
#include "service_lfs_spi_flash_adapter.h"
#include "bootloader_core.h"
#include "lfs.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"

#if MENU_ENABLE_AES_DECRYPT
#include "service_aes_decrypt.h"
#endif

#if MENU_ENABLE_HPATCH
#include "service_hpatch.h"
#endif

#if MENU_ENABLE_ESP8266_WIFI
#include "usart.h"
#include "esp8266_ota_api.h"
#include "esp8266_ota_config.h"
#include "service_wifi_transport.h"
#include "service_onenet_ota.h"
#endif

#if MENU_ENABLE_ED25519_VERIFY
#include "service_ed25519_verify.h"
#endif

#if MENU_ENABLE_RNG_DEVKEY
#include "rng.h"
#endif
#include "edsign.h"

#if MENU_ENABLE_FIRMWARE_PACKAGE
#include "firmware_package.h"
#include "hkdf.h"
#endif

#define MAX_FILES 20
#define MAX_FILENAME_LEN 128
#define RX_TIMEOUT ((uint32_t)0xFFFFFFFF)
#define USER_FLASH_SIZE (g_internal_flash.flash_base.total_size)

static menu_ctx_t g_menu_ctx;

static fs_fatfs_t g_fs_fatfs;
static fs_lfs_t g_fs_lfs;

#if MENU_ENABLE_FLASH_PROTECTION
uint32_t FlashProtection = 0;
#endif
uint8_t aFileName[FILE_NAME_LENGTH];

static char file_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t file_count = 0;

#if MENU_ENABLE_HPATCH
static char hdiff_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t hdiff_count = 0;
#endif

static uint8_t check_file_extension(const char *filename)
{
  const char *ext = NULL;
  size_t len = strlen(filename);

  if (len < 4)
    return 0;

  ext = filename + len - 4;
  if (strcmp(ext, ".bin") == 0 || strcmp(ext, ".BIN") == 0)
    return 1;
  if (strcmp(ext, ".sig") == 0 || strcmp(ext, ".SIG") == 0)
    return 3;

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

  if (len >= 8)
  {
    ext = filename + len - 8;
    if (strcmp(ext, ".iap.bin") == 0 || strcmp(ext, ".IAP.BIN") == 0)
      return 4;
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

#if MENU_ENABLE_HPATCH
static void scan_fs_hdiff_files(platform_fs_base_t *fs, const char *dir_path)
{
  platform_fs_dir_t dir;
  char name[MAX_FILENAME_LEN];
  uint32_t size;
  uint8_t is_dir;
  int16_t res;

  hdiff_count = 0;
  res = FS_DIR_OPEN(fs, &dir, dir_path);
  if (res != (int16_t)FS_STATUS_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot open directory!");
    return;
  }

  while (hdiff_count < MAX_FILES)
  {
    res = FS_DIR_READ(fs, &dir, name, &size, &is_dir);
    if (res != (int16_t)FS_STATUS_OK)
      break;
    if (!is_dir)
    {
      size_t len = strlen(name);
      if (len >= 6)
      {
        const char *ext = name + len - 6;
        if (strcmp(ext, ".hdiff") == 0 || strcmp(ext, ".HDIFF") == 0)
        {
          strncpy(hdiff_list[hdiff_count], name, MAX_FILENAME_LEN - 1);
          hdiff_list[hdiff_count][MAX_FILENAME_LEN - 1] = '\0';
          hdiff_count++;
        }
      }
    }
  }
  FS_DIR_CLOSE(fs, &dir);
}
#endif

static void scan_fs_files_filter(platform_fs_base_t *fs, const char *dir_path, uint8_t filter_type)
{
  platform_fs_dir_t dir;
  char name[MAX_FILENAME_LEN];
  uint32_t size;
  uint8_t is_dir;
  int16_t res;

  file_count = 0;
  res = FS_DIR_OPEN(fs, &dir, dir_path);
  if (res != (int16_t)FS_STATUS_OK)
  {
    menu_service_println(&g_menu_ctx, "Error: Cannot open directory!");
    return;
  }

  while (file_count < MAX_FILES)
  {
    res = FS_DIR_READ(fs, &dir, name, &size, &is_dir);
    if (res != (int16_t)FS_STATUS_OK)
      break;
    if (!is_dir)
    {
      uint8_t ext_type = check_file_extension(name);
      if (ext_type == filter_type || (filter_type == 0 && ext_type))
      {
        strncpy(file_list[file_count], name, MAX_FILENAME_LEN - 1);
        file_list[file_count][MAX_FILENAME_LEN - 1] = '\0';
        file_count++;
      }
    }
  }
  FS_DIR_CLOSE(fs, &dir);
}

#if MENU_ENABLE_DOWNLOAD
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

  err = bootloader_download(&g_fatfs_transport.base, &g_internal_flash.transport_base, bootloader_ctx.config.storage.fatfs_path);

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

  err = bootloader_download(&g_lfs_transport.base, &g_internal_flash.transport_base, bootloader_ctx.config.storage.lfs_path);

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
#endif

#if MENU_ENABLE_UPLOAD
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
#endif

#if MENU_ENABLE_EXECUTE_APP
static void cmd_execute_app(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Start program execution......\n");
  bootloader_ctx.config.jump.jump_func(bootloader_ctx.config.jump.app_jump_addr);
}
#endif

#if MENU_ENABLE_FLASH_PROTECTION
static void cmd_flash_protection(menu_ctx_t *ctx, int argc, char *argv[])
{
  FlashProtection = INTERNAL_FLASH_GET_PROTECTION_STATUS(&g_internal_flash.flash_base);

  if (FlashProtection != INTERNAL_FLASH_PROTECTION_NONE)
  {
    if (INTERNAL_FLASH_SET_PROTECTION(&g_internal_flash.flash_base, 0, 0) == INTERNAL_FLASH_STATUS_OK)
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
    if (INTERNAL_FLASH_SET_PROTECTION(&g_internal_flash.flash_base,
                                      OB_WRP_SECTOR_5 | OB_WRP_SECTOR_6 | OB_WRP_SECTOR_7 |
                                          OB_WRP_SECTOR_8 | OB_WRP_SECTOR_9 | OB_WRP_SECTOR_10 | OB_WRP_SECTOR_11,
                                      1) == INTERNAL_FLASH_STATUS_OK)
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
#endif

#if MENU_ENABLE_UART_PASSTHROUGH
static void cmd_uart_passthrough(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t rx_data;
  uint8_t q_count = 0;
  uint32_t q_timer = 0;
  platform_uart_base_t *uart4 = &g_uart4_console.base;
  platform_uart_base_t *usart1 = &g_usart1_esp8266.base;

  menu_service_println(ctx, "\r\n========== UART4 <-> USART1 Passthrough Mode ==========");
  menu_service_println(ctx, "  UART4 (PA0/PA1) <--> USART1 (PA9/PA10)");
  menu_service_println(ctx, "  Baud Rate: 115200, 8N1");
  menu_service_println(ctx, "  For ESP8266/BLE AT Command Debug");
  menu_service_println(ctx, "  Press 'q' 3 times within 1 second to exit");
  menu_service_println(ctx, "========================================================\n");

  UART_ABORT(usart1);
  UART_DISABLE_IT(usart1, PLATFORM_UART_IT_RXNE);
  UART_DISABLE_IT(usart1, PLATFORM_UART_IT_IDLE);
  UART_FLUSH(uart4);
  UART_FLUSH(usart1);
  UART_CLEAR_FLAG(uart4, PLATFORM_UART_FLAG_ORE);
  UART_CLEAR_FLAG(usart1, PLATFORM_UART_FLAG_ORE);

  while (1)
  {
    if (UART_GET_FLAG(uart4, PLATFORM_UART_FLAG_RXNE))
    {
      rx_data = UART_READ_BYTE(uart4);
      if (rx_data == 'q')
      {
        if (q_count == 0 || (HAL_GetTick() - q_timer) < 1000)
        {
          q_count++;
          q_timer = HAL_GetTick();
          if (q_count >= 3)
          {
            menu_service_println(ctx, "\n\nExiting passthrough mode...");
            UART_ENABLE_IT(usart1, PLATFORM_UART_IT_RXNE);
            UART_ENABLE_IT(usart1, PLATFORM_UART_IT_IDLE);
            return;
          }
        }
        else
        {
          q_count = 1;
          q_timer = HAL_GetTick();
        }
        while (!UART_GET_FLAG(usart1, PLATFORM_UART_FLAG_TXE))
          ;
        UART_WRITE_BYTE(usart1, rx_data);
      }
      else
      {
        q_count = 0;
        while (!UART_GET_FLAG(usart1, PLATFORM_UART_FLAG_TXE))
          ;
        UART_WRITE_BYTE(usart1, rx_data);
      }
    }
    else if (UART_GET_FLAG(uart4, PLATFORM_UART_FLAG_ORE))
    {
      UART_CLEAR_FLAG(uart4, PLATFORM_UART_FLAG_ORE);
    }

    if (UART_GET_FLAG(usart1, PLATFORM_UART_FLAG_RXNE))
    {
      rx_data = UART_READ_BYTE(usart1);
      while (!UART_GET_FLAG(uart4, PLATFORM_UART_FLAG_TXE))
        ;
      UART_WRITE_BYTE(uart4, rx_data);
    }
    else if (UART_GET_FLAG(usart1, PLATFORM_UART_FLAG_ORE))
    {
      UART_CLEAR_FLAG(usart1, PLATFORM_UART_FLAG_ORE);
    }
  }
}
#endif

#if MENU_ENABLE_SPI_FLASH_STORE
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

  menu_service_println(&g_menu_ctx, "Scanning TF card for bin, aes, hdiff and sig files...\r");
  scan_sd_card_files();

  if (file_count == 0)
  {
    menu_service_println(&g_menu_ctx, "No bin, aes, hdiff or sig files found on TF card!");
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

  menu_service_print(&g_menu_ctx, "\r\nEnter filename (with .bin, .bin.aes, .hdiff or .sig extension): ");
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
    menu_service_println(&g_menu_ctx, "\rError: Invalid file extension! Must be .bin, .bin.aes, .hdiff or .sig");
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
#endif

#if MENU_ENABLE_FIRMWARE_PACKAGE

#define FW_PKG_IAP_EXT ".iap.bin"
#define FW_PKG_IAP_EXT_UPPER ".IAP.BIN"

static uint8_t fw_pkg_is_iap_file(const char *filename)
{
  size_t len = strlen(filename);
  if (len >= 8)
  {
    const char *ext = filename + len - 8;
    if (strcmp(ext, FW_PKG_IAP_EXT) == 0 || strcmp(ext, FW_PKG_IAP_EXT_UPPER) == 0)
      return 1;
  }
  return 0;
}

static void fw_pkg_print_header_detail(menu_ctx_t *ctx, const fw_pkg_header_t *hdr)
{
  char msg[256];

  menu_service_println(ctx, "");
  menu_service_println(ctx, "======== Firmware Package Header ========");
  snprintf(msg, sizeof(msg), "  Magic:            0x%08lX", (unsigned long)hdr->magic);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Header Version:   %u", hdr->header_version);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Firmware Version:  v%u.%u.%u",
           hdr->firmware_major, hdr->firmware_minor, hdr->firmware_patch);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Payload Size:     %lu bytes", (unsigned long)hdr->total_payload_size);
  menu_service_println(ctx, msg);

  const char *img_type_str = "Unknown";
  switch (hdr->image_type)
  {
  case FW_PKG_IMAGE_APP:
    img_type_str = "App (0x01)";
    break;
  case FW_PKG_IMAGE_BOOTLOADER:
    img_type_str = "Bootloader (0x02)";
    break;
  case FW_PKG_IMAGE_RESOURCE:
    img_type_str = "Resource (0x03)";
    break;
  }
  snprintf(msg, sizeof(msg), "  Image Type:       %s", img_type_str);
  menu_service_println(ctx, msg);

  const char *enc_str = "None";
  switch (hdr->encryption_algo)
  {
  case FW_PKG_ENC_NONE:
    enc_str = "None (0x00)";
    break;
  case FW_PKG_ENC_AES256_CBC:
    enc_str = "AES-256-CBC (0x01)";
    break;
  case FW_PKG_ENC_AES256_ECB:
    enc_str = "AES-256-ECB (0x02)";
    break;
  case FW_PKG_ENC_AES256_CTR:
    enc_str = "AES-256-CTR (0x03)";
    break;
  }
  snprintf(msg, sizeof(msg), "  Encryption:       %s", enc_str);
  menu_service_println(ctx, msg);

  const char *sig_str = "None";
  if (hdr->signature_algo == FW_PKG_SIG_ED25519)
    sig_str = "Ed25519 (0x01)";
  snprintf(msg, sizeof(msg), "  Signature:        %s", sig_str);
  menu_service_println(ctx, msg);

  snprintf(msg, sizeof(msg), "  Hardware Compat:  0x%08lX", (unsigned long)hdr->hardware_compat);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Security Counter: %lu", (unsigned long)hdr->security_counter);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Build Timestamp:  %lu", (unsigned long)hdr->build_timestamp);
  menu_service_println(ctx, msg);

  static char hmac_hex[65];
  for (int i = 0; i < 32; i++)
    snprintf(&hmac_hex[i * 2], 3, "%02X", hdr->header_checksum[i]);
  hmac_hex[64] = '\0';
  snprintf(msg, sizeof(msg), "  HMAC:             %s...", hmac_hex);
  menu_service_println(ctx, msg);

  uint32_t cipher_size = hdr->total_payload_size - FW_PKG_SALT_SIZE - FW_PKG_IV_SIZE;
  snprintf(msg, sizeof(msg), "  Ciphertext Size:  %lu bytes", (unsigned long)cipher_size);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "==========================================");
}

static void fw_pkg_print_verify_config(menu_ctx_t *ctx, const fw_pkg_verify_config_t *config)
{
  char msg[256];
  static char hex_buf[65];

  menu_service_println(ctx, "");
  menu_service_println(ctx, "======== Verify Configuration ========");
  if (config->devkey)
  {
    for (size_t i = 0; i < config->devkey_len; i++)
      snprintf(&hex_buf[i * 2], 3, "%02X", config->devkey[i]);
    hex_buf[config->devkey_len * 2] = '\0';
    snprintf(msg, sizeof(msg), "  DevKey (%uB):     %s", (unsigned)config->devkey_len, hex_buf);
    menu_service_println(ctx, msg);
  }
  else
  {
    menu_service_println(ctx, "  DevKey:           NULL (no HMAC check)");
  }

  if (config->uid)
  {
    for (size_t i = 0; i < config->uid_len; i++)
      snprintf(&hex_buf[i * 2], 3, "%02X", config->uid[i]);
    hex_buf[config->uid_len * 2] = '\0';
    snprintf(msg, sizeof(msg), "  UID (%uB):        %s", (unsigned)config->uid_len, hex_buf);
    menu_service_println(ctx, msg);
  }

  if (config->ed25519_pubkey)
  {
    for (size_t i = 0; i < config->ed25519_pubkey_len && i < 8; i++)
      snprintf(&hex_buf[i * 2], 3, "%02X", config->ed25519_pubkey[i]);
    hex_buf[16] = '\0';
    snprintf(msg, sizeof(msg), "  PubKey (%uB):     %s...", (unsigned)config->ed25519_pubkey_len, hex_buf);
    menu_service_println(ctx, msg);
  }
  else
  {
    menu_service_println(ctx, "  PubKey:           NULL (no signature check)");
  }

  snprintf(msg, sizeof(msg), "  HW Compat:        0x%08lX", (unsigned long)config->hardware_compat);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Stored Counter:   %lu", (unsigned long)config->stored_security_counter);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "======================================");
}

static fw_pkg_err_t fw_pkg_process_and_save(
    menu_ctx_t *ctx,
    platform_fs_base_t *src_fs,
    const char *src_path,
    platform_fs_base_t *dst_fs,
    const char *dst_path,
    const fw_pkg_verify_config_t *config)
{
  platform_fs_file_t src_file, dst_file;
  fw_pkg_ctx_t pkg_ctx;
  int16_t fs_err;
  int32_t io_result;
  uint32_t total_read = 0;
  uint32_t total_written = 0;
  uint32_t bytes_read = 0;
  uint8_t aes_key[FW_PKG_AES_KEY_SIZE];
  struct sha512_state sig_state;
  uint8_t sig_pending[SHA512_BLOCK_SIZE];
  size_t sig_pending_len = 0;
  size_t sig_total_len = 0;
  char msg[256];

  static uint8_t process_buf[FW_PKG_DECRYPT_BUF_SIZE] __attribute__((aligned(4)));

  memset(&pkg_ctx, 0, sizeof(pkg_ctx));
  memset(aes_key, 0, sizeof(aes_key));

  if (config->ed25519_pubkey != NULL)
  {
    sha512_init(&sig_state);
    sig_pending_len = 0;
    sig_total_len = 0;
  }

  fs_err = FS_OPEN(src_fs, &src_file, src_path, FS_MODE_READ);
  if (fs_err != FS_STATUS_OK)
  {
    menu_service_println(ctx, "Error: Cannot open source file!");
    return FW_PKG_ERR_READ;
  }

  int32_t file_size = FS_SIZE(src_fs, &src_file);
  if (file_size < (int32_t)(FW_PKG_HEADER_SIZE + FW_PKG_SALT_SIZE + FW_PKG_IV_SIZE + FW_PKG_SIGNATURE_SIZE))
  {
    menu_service_println(ctx, "Error: File too small to be a valid package!");
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_SIZE;
  }

  snprintf(msg, sizeof(msg), "Source file size: %ld bytes", (long)file_size);
  menu_service_println(ctx, msg);

  menu_service_println(ctx, "Reading header (64 bytes)...");
  io_result = FS_READ(src_fs, &src_file, process_buf, FW_PKG_HEADER_SIZE);
  if (io_result != FW_PKG_HEADER_SIZE)
  {
    menu_service_println(ctx, "Error: Failed to read header!");
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_READ;
  }
  total_read += io_result;

  fw_pkg_err_t ret = fw_pkg_parse_header(&pkg_ctx, process_buf, io_result);
  if (ret != FW_PKG_OK)
  {
    snprintf(msg, sizeof(msg), "Error: Header parse failed: %s", fw_pkg_err_str(ret));
    menu_service_println(ctx, msg);
    FS_CLOSE(src_fs, &src_file);
    return ret;
  }

  fw_pkg_print_header_detail(ctx, &pkg_ctx.header);

  if (config->ed25519_pubkey != NULL)
  {
    fw_pkg_sha512_feed(&sig_state, process_buf, FW_PKG_HEADER_SIZE, sig_pending, &sig_pending_len);
    sig_total_len += FW_PKG_HEADER_SIZE;
  }

  if (config->devkey != NULL)
  {
    menu_service_println(ctx, "Verifying HMAC...");
    ret = fw_pkg_verify_header_hmac(&pkg_ctx, config);
    if (ret != FW_PKG_OK)
    {
      menu_service_println(ctx, "FAILED: HMAC verification failed!");
      FS_CLOSE(src_fs, &src_file);
      return ret;
    }
    menu_service_println(ctx, "HMAC verification: PASSED");
  }
  else
  {
    menu_service_println(ctx, "HMAC verification: SKIPPED (no DevKey)");
  }

  if (pkg_ctx.header.hardware_compat != config->hardware_compat)
  {
    snprintf(msg, sizeof(msg), "FAILED: HW compat mismatch (pkg=0x%08lX, board=0x%08lX)",
             (unsigned long)pkg_ctx.header.hardware_compat, (unsigned long)config->hardware_compat);
    menu_service_println(ctx, msg);
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_HW_COMPAT;
  }
  menu_service_println(ctx, "HW compat check: PASSED");

  ret = fw_pkg_check_rollback(&pkg_ctx, config);
  if (ret != FW_PKG_OK)
  {
    menu_service_println(ctx, "FAILED: Rollback detected!");
    FS_CLOSE(src_fs, &src_file);
    return ret;
  }
  menu_service_println(ctx, "Security counter check: PASSED");

  menu_service_println(ctx, "Reading DynamicSalt (16 bytes)...");
  io_result = FS_READ(src_fs, &src_file, pkg_ctx.dynamic_salt, FW_PKG_SALT_SIZE);
  if (io_result != FW_PKG_SALT_SIZE)
  {
    menu_service_println(ctx, "Error: Failed to read salt!");
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_READ;
  }
  total_read += io_result;

  {
    static char salt_hex[33];
    for (int i = 0; i < 16; i++)
      snprintf(&salt_hex[i * 2], 3, "%02X", pkg_ctx.dynamic_salt[i]);
    salt_hex[32] = '\0';
    snprintf(msg, sizeof(msg), "  DynamicSalt: %s", salt_hex);
    menu_service_println(ctx, msg);
  }

  if (config->ed25519_pubkey != NULL)
  {
    fw_pkg_sha512_feed(&sig_state, pkg_ctx.dynamic_salt, FW_PKG_SALT_SIZE, sig_pending, &sig_pending_len);
    sig_total_len += FW_PKG_SALT_SIZE;
  }

  menu_service_println(ctx, "Reading IV (16 bytes)...");
  io_result = FS_READ(src_fs, &src_file, pkg_ctx.iv, FW_PKG_IV_SIZE);
  if (io_result != FW_PKG_IV_SIZE)
  {
    menu_service_println(ctx, "Error: Failed to read IV!");
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_READ;
  }
  total_read += io_result;

  {
    static char iv_hex[33];
    for (int i = 0; i < 16; i++)
      snprintf(&iv_hex[i * 2], 3, "%02X", pkg_ctx.iv[i]);
    iv_hex[32] = '\0';
    snprintf(msg, sizeof(msg), "  IV:           %s", iv_hex);
    menu_service_println(ctx, msg);
  }

  if (config->ed25519_pubkey != NULL)
  {
    fw_pkg_sha512_feed(&sig_state, pkg_ctx.iv, FW_PKG_IV_SIZE, sig_pending, &sig_pending_len);
    sig_total_len += FW_PKG_IV_SIZE;
  }

  if (pkg_ctx.header.encryption_algo != FW_PKG_ENC_NONE)
  {
    menu_service_println(ctx, "Deriving AES key via HKDF...");
    ret = fw_pkg_derive_aes_key(&pkg_ctx, config, aes_key);
    if (ret != FW_PKG_OK)
    {
      menu_service_println(ctx, "Error: Key derivation failed!");
      FS_CLOSE(src_fs, &src_file);
      return ret;
    }

    {
      static char key_hex[65];
      for (int i = 0; i < 32; i++)
        snprintf(&key_hex[i * 2], 3, "%02X", aes_key[i]);
      key_hex[64] = '\0';
      snprintf(msg, sizeof(msg), "  AES-256 Key:  %s", key_hex);
      menu_service_println(ctx, msg);
    }

    ret = fw_pkg_decrypt_init(&pkg_ctx, aes_key);
    if (ret != FW_PKG_OK)
    {
      menu_service_println(ctx, "Error: AES context init failed!");
      FS_CLOSE(src_fs, &src_file);
      return ret;
    }
  }

  menu_service_println(ctx, "Opening output file for decrypted data...");
  fs_err = FS_OPEN(dst_fs, &dst_file, dst_path, FS_MODE_CREATE_ALWAYS);
  if (fs_err != FS_STATUS_OK)
  {
    snprintf(msg, sizeof(msg), "Error: Cannot create output file: %s", dst_path);
    menu_service_println(ctx, msg);
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_WRITE;
  }

  snprintf(msg, sizeof(msg), "Output: %s", dst_path);
  menu_service_println(ctx, msg);

  uint32_t ciphertext_remaining = pkg_ctx.ciphertext_size;
  uint32_t signature_offset = (uint32_t)file_size - FW_PKG_SIGNATURE_SIZE;

  menu_service_println(ctx, "Processing ciphertext...");

  while (ciphertext_remaining > 0)
  {
    uint32_t to_read = FW_PKG_DECRYPT_BUF_SIZE;
    if (ciphertext_remaining < to_read)
      to_read = ciphertext_remaining;
    if (total_read + to_read > signature_offset)
    {
      to_read = signature_offset - total_read;
      if (to_read == 0)
        break;
    }

    io_result = FS_READ(src_fs, &src_file, process_buf, to_read);
    if (io_result <= 0)
      break;

    bytes_read = (uint32_t)io_result;
    total_read += bytes_read;

    if (config->ed25519_pubkey != NULL)
    {
      fw_pkg_sha512_feed(&sig_state, process_buf, bytes_read, sig_pending, &sig_pending_len);
      sig_total_len += bytes_read;
    }

    if (pkg_ctx.header.encryption_algo != FW_PKG_ENC_NONE)
    {
      ret = fw_pkg_decrypt_payload(&pkg_ctx, process_buf, bytes_read);
      if (ret != FW_PKG_OK)
      {
        menu_service_println(ctx, "Error: Decryption failed!");
        FS_CLOSE(dst_fs, &dst_file);
        FS_CLOSE(src_fs, &src_file);
        return ret;
      }
    }

    io_result = FS_WRITE(dst_fs, &dst_file, process_buf, bytes_read);
    if (io_result != (int32_t)bytes_read)
    {
      menu_service_println(ctx, "Error: Write to output file failed!");
      FS_CLOSE(dst_fs, &dst_file);
      FS_CLOSE(src_fs, &src_file);
      return FW_PKG_ERR_WRITE;
    }
    total_written += bytes_read;
    ciphertext_remaining -= bytes_read;

    if ((total_written % 32768) == 0 || ciphertext_remaining == 0)
    {
      snprintf(msg, sizeof(msg), "  Progress: %lu / %lu bytes",
               (unsigned long)total_written, (unsigned long)pkg_ctx.ciphertext_size);
      menu_service_println(ctx, msg);
    }
  }

  if (total_read < signature_offset)
  {
    uint32_t skip = signature_offset - total_read;
    while (skip > 0)
    {
      uint32_t to_skip = (skip > FW_PKG_DECRYPT_BUF_SIZE) ? FW_PKG_DECRYPT_BUF_SIZE : skip;
      io_result = FS_READ(src_fs, &src_file, process_buf, to_skip);
      if (io_result <= 0)
        break;
      bytes_read = (uint32_t)io_result;
      if (config->ed25519_pubkey != NULL)
      {
        fw_pkg_sha512_feed(&sig_state, process_buf, bytes_read, sig_pending, &sig_pending_len);
        sig_total_len += bytes_read;
      }
      total_read += bytes_read;
      skip -= bytes_read;
    }
  }

  FS_SYNC(dst_fs, &dst_file);
  FS_CLOSE(dst_fs, &dst_file);

  snprintf(msg, sizeof(msg), "Decrypted data saved: %lu bytes", (unsigned long)total_written);
  menu_service_println(ctx, msg);

  menu_service_println(ctx, "Reading signature (64 bytes)...");
  io_result = FS_READ(src_fs, &src_file, pkg_ctx.signature, FW_PKG_SIGNATURE_SIZE);
  if (io_result != FW_PKG_SIGNATURE_SIZE)
  {
    menu_service_println(ctx, "Error: Failed to read signature!");
    FS_CLOSE(src_fs, &src_file);
    return FW_PKG_ERR_READ;
  }

  {
    static char sig_hex[33];
    for (int i = 0; i < 16; i++)
      snprintf(&sig_hex[i * 2], 3, "%02X", pkg_ctx.signature[i]);
    sig_hex[32] = '\0';
    snprintf(msg, sizeof(msg), "  Signature:    %s...", sig_hex);
    menu_service_println(ctx, msg);
  }

  FS_CLOSE(src_fs, &src_file);

  if (pkg_ctx.header.signature_algo == FW_PKG_SIG_ED25519 && config->ed25519_pubkey != NULL)
  {
    menu_service_println(ctx, "Verifying Ed25519 signature...");
    uint8_t sig_hash[64];
    fw_pkg_sha512_finish(&sig_state, sig_pending, sig_pending_len, sig_total_len, sig_hash);

    ret = fw_pkg_verify_signature_hash(&pkg_ctx, config, sig_hash);
    if (ret != FW_PKG_OK)
    {
      menu_service_println(ctx, "FAILED: Ed25519 signature verification FAILED!");
      menu_service_println(ctx, "Firmware may be TAMPERED or CORRUPTED!");
      return ret;
    }
    menu_service_println(ctx, "Ed25519 signature: VERIFIED");
  }
  else
  {
    menu_service_println(ctx, "Signature verification: SKIPPED (no public key)");
  }

  menu_service_println(ctx, "");
  menu_service_println(ctx, "======== Summary ========");
  snprintf(msg, sizeof(msg), "  Firmware:     v%u.%u.%u",
           pkg_ctx.header.firmware_major, pkg_ctx.header.firmware_minor, pkg_ctx.header.firmware_patch);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Ciphertext:   %lu bytes", (unsigned long)pkg_ctx.ciphertext_size);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Decrypted:    %lu bytes", (unsigned long)total_written);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "  Output:       %s", dst_path);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "  HMAC:         PASSED");
  if (config->ed25519_pubkey)
    menu_service_println(ctx, "  Signature:    VERIFIED");
  else
    menu_service_println(ctx, "  Signature:    SKIPPED");
  menu_service_println(ctx, "  Result:       SUCCESS");
  menu_service_println(ctx, "=========================");

  return FW_PKG_OK;
}

static void cmd_fw_pkg_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  FRESULT res;
  char src_path[128];
  char dst_path[128];
  char *dot_pos;

  uint8_t devkey_bytes[16];
  uint8_t uid_bytes[12];
  int j;

  menu_service_println(ctx, "");
  menu_service_println(ctx, "====== Firmware Package Parse (SD Card) ======");

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Scanning for .iap.bin files...");
  scan_sd_card_files();

  uint8_t iap_count = 0;
  for (i = 0; i < file_count; i++)
  {
    if (fw_pkg_is_iap_file(file_list[i]))
    {
      snprintf(msg, sizeof(msg), "  [%d] %s", iap_count + 1, file_list[i]);
      menu_service_println(ctx, msg);
      iap_count++;
    }
  }

  if (iap_count == 0)
  {
    menu_service_println(ctx, "No .iap.bin files found on SD card!");
    f_mount(NULL, (TCHAR const *)SDPath, 0);
    return;
  }

  menu_service_printf(ctx, "\r\nSelect file (1-%d) or 'a' to abort: ", iap_count);
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
      if (selected >= 1 && selected <= iap_count)
        break;
    }
  }

  uint8_t iap_idx = 0;
  for (i = 0; i < file_count; i++)
  {
    if (fw_pkg_is_iap_file(file_list[i]))
    {
      iap_idx++;
      if (iap_idx == selected)
      {
        selected = i;
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected]);
  menu_service_println(ctx, msg);

  snprintf(src_path, sizeof(src_path), "0:/%s", file_list[selected]);
  strncpy(dst_path, src_path, sizeof(dst_path) - 1);
  dst_path[sizeof(dst_path) - 1] = '\0';

  dot_pos = strstr(dst_path, FW_PKG_IAP_EXT);
  if (dot_pos == NULL)
    dot_pos = strstr(dst_path, FW_PKG_IAP_EXT_UPPER);
  if (dot_pos != NULL)
  {
    *dot_pos = '\0';
    strcat(dst_path, ".dec.bin");
  }
  else
  {
    strcat(dst_path, ".decrypted");
  }

  {
    const uint8_t *otp_ptr = (const uint8_t *)FLASH_OTP_BASE;
    memcpy(devkey_bytes, otp_ptr, 16);
  }

  uint8_t *uid_ptr = (uint8_t *)UID_BASE;
  memcpy(uid_bytes, uid_ptr, 12);

  fw_pkg_verify_config_t verify_config = {
      .devkey = devkey_bytes,
      .devkey_len = 16,
      .uid = uid_bytes,
      .uid_len = 12,
      .ed25519_pubkey = FW_PKG_ED25519_PUBLIC_KEY,
      .ed25519_pubkey_len = 32,
      .hardware_compat = 0,
      .stored_security_counter = 0,
  };

  fw_pkg_print_verify_config(ctx, &verify_config);

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");

  fw_pkg_err_t ret = fw_pkg_process_and_save(ctx, &g_fs_fatfs.base, src_path,
                                             &g_fs_fatfs.base, dst_path, &verify_config);

  if (ret != FW_PKG_OK)
  {
    snprintf(msg, sizeof(msg), "Firmware package parse FAILED: %s (err=%d)", fw_pkg_err_str(ret), ret);
    menu_service_println(ctx, msg);
  }

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_fw_pkg_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[256];
  char src_path[128];
  char dst_path[128];
  char *dot_pos;

  uint8_t devkey_bytes[16];
  uint8_t uid_bytes[12];
  int j;
  lfs_t lfs;

  menu_service_println(ctx, "");
  menu_service_println(ctx, "====== Firmware Package Parse (SPI Flash) ======");

  menu_service_println(ctx, "Initializing SPI Flash...");
  if (lfs_spi_flash_init() != 0)
  {
    menu_service_println(ctx, "Error: SPI Flash initialization failed!");
    return;
  }

  menu_service_println(ctx, "Mounting LittleFS...");
  if (lfs_spi_flash_mount(&lfs) != LFS_ERR_OK)
  {
    menu_service_println(ctx, "Error: LittleFS mount failed!");
    return;
  }

  menu_service_println(ctx, "Scanning for .iap.bin files...");
  scan_lfs_files(&lfs);

  uint8_t iap_count = 0;
  for (i = 0; i < file_count; i++)
  {
    if (fw_pkg_is_iap_file(file_list[i]))
    {
      snprintf(msg, sizeof(msg), "  [%d] %s", iap_count + 1, file_list[i]);
      menu_service_println(ctx, msg);
      iap_count++;
    }
  }

  if (iap_count == 0)
  {
    menu_service_println(ctx, "No .iap.bin files found on SPI Flash!");
    return;
  }

  menu_service_printf(ctx, "\r\nSelect file (1-%d) or 'a' to abort: ", iap_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
      return;
    }
    if (key >= '1' && key <= '9')
    {
      selected = key - '0';
      if (selected >= 1 && selected <= iap_count)
        break;
    }
  }

  uint8_t iap_idx = 0;
  for (i = 0; i < file_count; i++)
  {
    if (fw_pkg_is_iap_file(file_list[i]))
    {
      iap_idx++;
      if (iap_idx == selected)
      {
        selected = i;
        break;
      }
    }
  }

  snprintf(msg, sizeof(msg), "\rSelected: %s", file_list[selected]);
  menu_service_println(ctx, msg);

  snprintf(src_path, sizeof(src_path), "/%s", file_list[selected]);
  strncpy(dst_path, src_path, sizeof(dst_path) - 1);
  dst_path[sizeof(dst_path) - 1] = '\0';

  dot_pos = strstr(dst_path, FW_PKG_IAP_EXT);
  if (dot_pos == NULL)
    dot_pos = strstr(dst_path, FW_PKG_IAP_EXT_UPPER);
  if (dot_pos != NULL)
  {
    *dot_pos = '\0';
    strcat(dst_path, ".dec.bin");
  }
  else
  {
    strcat(dst_path, ".decrypted");
  }

  {
    const uint8_t *otp_ptr = (const uint8_t *)FLASH_OTP_BASE;
    memcpy(devkey_bytes, otp_ptr, 16);
  }

  uint8_t *uid_ptr = (uint8_t *)UID_BASE;
  memcpy(uid_bytes, uid_ptr, 12);

  fw_pkg_verify_config_t verify_config = {
      .devkey = devkey_bytes,
      .devkey_len = 16,
      .uid = uid_bytes,
      .uid_len = 12,
      .ed25519_pubkey = FW_PKG_ED25519_PUBLIC_KEY,
      .ed25519_pubkey_len = 32,
      .hardware_compat = 0,
      .stored_security_counter = 0,
  };

  fw_pkg_print_verify_config(ctx, &verify_config);

  platform_fs_lfs_register(&g_fs_lfs, &lfs, "lfs");

  fw_pkg_err_t ret = fw_pkg_process_and_save(ctx, &g_fs_lfs.base, src_path,
                                             &g_fs_lfs.base, dst_path, &verify_config);

  if (ret != FW_PKG_OK)
  {
    snprintf(msg, sizeof(msg), "Firmware package parse FAILED: %s (err=%d)", fw_pkg_err_str(ret), ret);
    menu_service_println(ctx, msg);
  }
}

#endif

#if MENU_ENABLE_ESP8266_WIFI
static void Print_Current_Time(void)
{
  platform_rtc_time_t rtc_time;
  platform_rtc_date_t rtc_date;
  char msg[128];

  if (RTC_GET_TIME(&g_rtc.base, &rtc_time) != RTC_STATUS_OK)
  {
    menu_service_println(&g_menu_ctx, "Failed to get RTC time!");
    return;
  }

  if (RTC_GET_DATE(&g_rtc.base, &rtc_date) != RTC_STATUS_OK)
  {
    menu_service_println(&g_menu_ctx, "Failed to get RTC date!");
    return;
  }

  if (rtc_date.year < 2025)
  {
    menu_service_println(&g_menu_ctx, "RTC time not set (year < 2025)");
    return;
  }

  snprintf(msg, sizeof(msg), "\rCurrent Time: %04d-%02d-%02d %02d:%02d:%02d (UTC+8)",
           rtc_date.year, rtc_date.month, rtc_date.date,
           rtc_time.hours, rtc_time.minutes, rtc_time.seconds);
  menu_service_println(&g_menu_ctx, msg);

  uint32_t timestamp;
  if (RTC_GET_TIMESTAMP(&g_rtc.base, &timestamp) == RTC_STATUS_OK)
  {
    snprintf(msg, sizeof(msg), "Unix Timestamp: %lu", (unsigned long)timestamp);
    menu_service_println(&g_menu_ctx, msg);
  }
}

static void cmd_esp8266_init(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[128];

  menu_service_println(ctx, "Initializing ESP8266...");

  menu_service_println(ctx, "Step 1: AT test...");
  if (WIFI_AT_TEST(&g_esp8266_wifi.base) == PLATFORM_WIFI_OK)
    menu_service_println(ctx, "  AT test OK!");
  else
  {
    menu_service_println(ctx, "  AT test FAILED!");
    return;
  }

  char ip_buf[48] = {0};
  if (WIFI_GET_IP(&g_esp8266_wifi.base, ip_buf, sizeof(ip_buf)) == PLATFORM_WIFI_OK && ip_buf[0] != '\0')
  {
    snprintf(msg, sizeof(msg), "  Already connected! IP: %s", ip_buf);
    menu_service_println(ctx, msg);
    menu_service_println(ctx, "Step 2: Init OTA context...");
    esp8266_ota_init();
    menu_service_println(ctx, "ESP8266 init done!");
    return;
  }

  menu_service_println(ctx, "Step 2: Set STA mode...");
  if (WIFI_SET_MODE(&g_esp8266_wifi.base, PLATFORM_WIFI_MODE_STA) == PLATFORM_WIFI_OK)
    menu_service_println(ctx, "  STA mode OK!");
  else
  {
    menu_service_println(ctx, "  Set STA mode FAILED!");
    return;
  }

  snprintf(msg, sizeof(msg), "Step 3: Join AP %s...", ESP8266_WIFI_SSID);
  menu_service_println(ctx, msg);
  if (WIFI_JOIN_AP(&g_esp8266_wifi.base, ESP8266_WIFI_SSID, ESP8266_WIFI_PASSWORD) == PLATFORM_WIFI_OK)
  {
    menu_service_println(ctx, "  Join AP OK!");
    if (WIFI_GET_IP(&g_esp8266_wifi.base, ip_buf, sizeof(ip_buf)) == PLATFORM_WIFI_OK)
    {
      snprintf(msg, sizeof(msg), "  IP: %s", ip_buf);
      menu_service_println(ctx, msg);
    }
  }
  else
  {
    menu_service_println(ctx, "  Join AP FAILED!");
    return;
  }

  menu_service_println(ctx, "Step 4: Init OTA context...");
  esp8266_ota_init();
  menu_service_println(ctx, "ESP8266 init done!");
}

static void cmd_esp8266_at_test(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Testing AT command...");
  if (WIFI_AT_TEST(&g_esp8266_wifi.base) == PLATFORM_WIFI_OK)
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

  if (WIFI_CONNECT_TCP(&g_esp8266_wifi.base, server_ip, (uint16_t)atoi(server_port)) == PLATFORM_WIFI_OK)
    menu_service_println(ctx, "TCP connect OK!");
  else
    menu_service_println(ctx, "TCP connect FAILED!");
}

static void cmd_esp8266_transparent_enter(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Entering transparent mode...");
  if (WIFI_ENTER_TRANSPARENT(&g_esp8266_wifi.base) == PLATFORM_WIFI_OK)
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
  WIFI_EXIT_TRANSPARENT(&g_esp8266_wifi.base);
  menu_service_println(ctx, "Transparent mode exited.");
}

static void cmd_ota_target_internal(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to Internal Flash...");
  esp8266_ota_set_target_internal_flash();
}

static void cmd_ota_target_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to SD Card (FATFS)...");
  esp8266_ota_set_target_sd_card();
}

static void cmd_ota_target_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Setting OTA target to SPI Flash (LFS)...");
  esp8266_ota_set_target_spi_flash();
}

static void ota_menu_progress_cb(const onenet_ota_package_info_t *info, int progress)
{
  menu_service_print_progress(&g_menu_ctx, "OTA Downloading...", progress);
}

static void cmd_onenet_ota_download(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Starting OneNET OTA download...");
  esp8266_ota_set_progress_callback(ota_menu_progress_cb);
  esp8266_ota_download();
  esp8266_ota_set_progress_callback(NULL);
}

static void cmd_show_time(menu_ctx_t *ctx, int argc, char *argv[])
{
  Print_Current_Time();
}

static void cmd_mqtt_check_status(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];

  menu_service_println(ctx, "Checking MQTT connection status...");
  int16_t ret = MQTT_CHECK_CONNECTED(&g_esp8266_mqtt.base, 0);
  if (ret == PLATFORM_MQTT_OK)
  {
    menu_service_println(ctx, "MQTT: Connected");
  }
  else
  {
    menu_service_println(ctx, "MQTT: Not connected");
  }
}

static void cmd_mqtt_configure(menu_ctx_t *ctx, int argc, char *argv[])
{
  platform_mqtt_user_config_t config;
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

  if (MQTT_USERCFG(&g_esp8266_mqtt.base, 0, &config) == PLATFORM_MQTT_OK)
    menu_service_println(ctx, "MQTT user config OK!");
  else
    menu_service_println(ctx, "MQTT user config FAILED!");
}

static void cmd_mqtt_connect(menu_ctx_t *ctx, int argc, char *argv[])
{
  char msg[256];
  char host[64] = ONENET_MQTT_HOST;
  uint16_t port = ONENET_MQTT_PORT;
  uint8_t key = 0;
  uint8_t idx = 0;
  char input_buf[128];

  menu_service_println(ctx, "Checking if already connected...");
  int16_t ret = MQTT_CHECK_CONNECTED(&g_esp8266_mqtt.base, 0);
  if (ret == PLATFORM_MQTT_OK)
  {
    menu_service_println(ctx, "MQTT: Already connected!");
    return;
  }

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

  if (MQTT_CONNECT(&g_esp8266_mqtt.base, 0, host, port, 1) == PLATFORM_MQTT_OK)
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
  MQTT_SUBSCRIBE(&g_esp8266_mqtt.base, 0, topic, 1);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  MQTT_SUBSCRIBE(&g_esp8266_mqtt.base, 0, topic, 1);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/get/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  MQTT_SUBSCRIBE(&g_esp8266_mqtt.base, 0, topic, 1);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/desired/delete/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  MQTT_SUBSCRIBE(&g_esp8266_mqtt.base, 0, topic, 1);

  snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/get", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
  snprintf(msg, sizeof(msg), "Subscribing: %s", topic);
  menu_service_println(ctx, msg);
  MQTT_SUBSCRIBE(&g_esp8266_mqtt.base, 0, topic, 1);
}

static void cmd_mqtt_publish(menu_ctx_t *ctx, int argc, char *argv[])
{
  platform_mqtt_property_t prop;
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

  if (prop.value_type == PLATFORM_MQTT_VALUE_FLOAT)
    prop.value_float = atof(value_buf);
  else if (prop.value_type == PLATFORM_MQTT_VALUE_BOOL)
    prop.value_int = (strcmp(value_buf, "true") == 0 || strcmp(value_buf, "1") == 0) ? 1 : 0;
  else if (prop.value_type == PLATFORM_MQTT_VALUE_STRING)
    strncpy(prop.id, value_buf, sizeof(prop.id) - 1);
  else
    prop.value_int = atoi(value_buf);

  snprintf(msg, sizeof(msg), "\rPublishing: %s = %s (type=%d, id=%s)", prop.key, value_buf, prop.value_type, msg_id);
  menu_service_println(ctx, msg);

  if (MQTT_PUBLISH_PROPERTY(&g_esp8266_mqtt.base, 0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, &prop, 1, msg_id) == PLATFORM_MQTT_OK)
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

    char recv_topic[PLATFORM_MQTT_MAX_TOPIC_LEN];
    char recv_payload[PLATFORM_MQTT_MAX_PAYLOAD_LEN];
    char recv_msg_id[32];
    platform_mqtt_property_t recv_props[8];
    uint8_t recv_prop_count = 0;

    int16_t recv_ret = MQTT_CHECK_PROPERTY_SET_RECV(&g_esp8266_mqtt.base, recv_topic, recv_payload, sizeof(recv_payload), recv_msg_id);
    if (recv_ret == PLATFORM_MQTT_OK)
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
        if (MQTT_PUBLISH_SET_REPLY(&g_esp8266_mqtt.base, 0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, recv_msg_id, 200, "user_succ") == PLATFORM_MQTT_OK)
          menu_service_println(ctx, "Reply sent successfully!");
        else
          menu_service_println(ctx, "Reply FAILED!");
      }
    }
    HAL_Delay(10);
  }
}

static void cmd_mqtt_disconnect(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Disconnecting MQTT...");
  if (MQTT_DISCONNECT(&g_esp8266_mqtt.base, 0) == PLATFORM_MQTT_OK)
    menu_service_println(ctx, "MQTT disconnected!");
  else
    menu_service_println(ctx, "Disconnect FAILED!");
}

static void cmd_mqtt_sync_time(menu_ctx_t *ctx, int argc, char *argv[])
{
  menu_service_println(ctx, "Syncing time from server...");
  if (esp8266_ota_sync_time())
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

    platform_rtc_time_t rtc_time;
    platform_rtc_date_t rtc_date;

    if (RTC_GET_TIME(&g_rtc.base, &rtc_time) == RTC_STATUS_OK &&
        RTC_GET_DATE(&g_rtc.base, &rtc_date) == RTC_STATUS_OK)
    {
      snprintf(rtc_time_str, sizeof(rtc_time_str), "%04d-%02d-%02d %02d:%02d:%02d",
               rtc_date.year, rtc_date.month, rtc_date.date,
               rtc_time.hours, rtc_time.minutes, rtc_time.seconds);

      platform_mqtt_property_t prop;
      memset(&prop, 0, sizeof(prop));
      strncpy(prop.key, "RTC_TIME", sizeof(prop.key) - 1);
      strncpy(prop.id, rtc_time_str, sizeof(prop.id) - 1);
      prop.value_type = PLATFORM_MQTT_VALUE_STRING;

      if (MQTT_PUBLISH_PROPERTY(&g_esp8266_mqtt.base, 0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, &prop, 1, "007") == PLATFORM_MQTT_OK)
      {
        publish_count++;
        snprintf(msg, sizeof(msg), "Published %lu times: %s", (unsigned long)publish_count, rtc_time_str);
        menu_service_println(ctx, msg);
      }
    }

    HAL_Delay(1000);
  }
}
#endif

#if MENU_ENABLE_AES_DECRYPT
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

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");
  decrypt_result = aes_decrypt_file(&g_fs_fatfs.base, src_path, dst_path, &decrypt_config);

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

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");
  decrypt_result = aes_decrypt_to_flash(&g_fs_fatfs.base, src_path, &g_internal_flash.transport_base, &decrypt_config);

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

  platform_fs_lfs_register(&g_fs_lfs, &lfs, "lfs");
  decrypt_result = aes_decrypt_to_flash(&g_fs_lfs.base, file_list[selected], &g_internal_flash.transport_base, &decrypt_config);

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
#endif

#if MENU_ENABLE_HPATCH
typedef struct
{
  const char *storage_name;
  const char *dir_path;
  const char *path_prefix;
  platform_fs_base_t *fs;
} hpatch_storage_ctx_t;

static void cmd_hpatch_workflow(menu_ctx_t *ctx, const hpatch_storage_ctx_t *storage)
{
  uint8_t key = 0;
  uint8_t selected_diff = 0;
  uint8_t selected_old = 0;
  uint8_t i;
  static char msg[256];
  hpatch_err_t patch_result;
  hpatch_config_t config;
  static char diff_path[HPATCH_MAX_PATH_LEN];
  static char old_path[HPATCH_MAX_PATH_LEN];
  static char out_path[HPATCH_MAX_PATH_LEN];
  char *dot_pos;
  const char *upgrade_tag = "_HdiffUpgraded";

  menu_service_println(ctx, "Scanning for .hdiff files...\r");
  scan_fs_hdiff_files(storage->fs, storage->dir_path);

  if (hdiff_count == 0)
  {
    snprintf(msg, sizeof(msg), "No .hdiff files found on %s!", storage->storage_name);
    menu_service_println(ctx, msg);
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
  scan_fs_files_filter(storage->fs, storage->dir_path, 1);

  if (file_count == 0)
  {
    snprintf(msg, sizeof(msg), "No .bin files found on %s!", storage->storage_name);
    menu_service_println(ctx, msg);
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

  if (storage->path_prefix[0] != '\0')
  {
    snprintf(diff_path, sizeof(diff_path), "%s%s", storage->path_prefix, hdiff_list[selected_diff - 1]);
    snprintf(old_path, sizeof(old_path), "%s%s", storage->path_prefix, file_list[selected_old - 1]);
  }
  else
  {
    strncpy(diff_path, hdiff_list[selected_diff - 1], sizeof(diff_path) - 1);
    diff_path[sizeof(diff_path) - 1] = '\0';
    strncpy(old_path, file_list[selected_old - 1], sizeof(old_path) - 1);
    old_path[sizeof(old_path) - 1] = '\0';
  }

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

  config.fs = storage->fs;
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
    snprintf(msg, sizeof(msg), "Upgraded firmware saved to %s.", storage->storage_name);
    menu_service_println(ctx, msg);
    snprintf(msg, sizeof(msg), "Output: %s", config.out_path);
    menu_service_println(ctx, msg);
  }
  else
  {
    menu_service_print(ctx, "HPatch upgrade failed! Error: ");
    menu_service_println(ctx, hpatch_err_to_string(patch_result));
  }
}

static void cmd_hpatch_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  FRESULT res;
  char msg[256];
  hpatch_storage_ctx_t storage = {"SD card", "0:/", "0:/", NULL};

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");
  storage.fs = &g_fs_fatfs.base;

  cmd_hpatch_workflow(ctx, &storage);

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_hpatch_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  int res;
  lfs_t lfs;
  hpatch_storage_ctx_t storage = {"SPI Flash", "/", "", NULL};

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

  platform_fs_lfs_register(&g_fs_lfs, &lfs, "lfs");
  storage.fs = &g_fs_lfs.base;

  cmd_hpatch_workflow(ctx, &storage);

  lfs_spi_flash_unmount(&lfs);
}
#endif

#if MENU_ENABLE_ED25519_VERIFY
typedef struct
{
  const char *storage_name;
  const char *dir_path;
  const char *path_prefix;
  platform_fs_base_t *fs;
} ed25519_storage_ctx_t;

static void cmd_ed25519_verify_workflow(menu_ctx_t *ctx, const ed25519_storage_ctx_t *storage)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  static char msg[256];
  int verify_result;
  static char data_path[128];
  static char sig_path[128];
  ed25519_verify_config_t verify_config = {
      .public_key = {0x59, 0x89, 0x1c, 0x71, 0x9e, 0xd1, 0xa1, 0x95, 0x7b, 0x6f, 0x1e, 0x77, 0x6e, 0x1f, 0xf0, 0xf6, 0xff, 0xf3, 0xc3, 0x60, 0x62, 0xe7, 0xcc, 0x22, 0x8c, 0xa1, 0x76, 0x88, 0x0e, 0xe7, 0x2b, 0xde}};

  menu_service_println(ctx, "Scanning for .bin files...\r");
  scan_fs_files_filter(storage->fs, storage->dir_path, 1);

  if (file_count == 0)
  {
    snprintf(msg, sizeof(msg), "No .bin files found on %s!", storage->storage_name);
    menu_service_println(ctx, msg);
    return;
  }

  menu_service_println(ctx, "Found firmware files:");
  for (i = 0; i < file_count; i++)
  {
    snprintf(msg, sizeof(msg), "  [%d] %s", i + 1, file_list[i]);
    menu_service_println(ctx, msg);
  }

  menu_service_printf(ctx, "\r\nSelect firmware file to verify (1-%d) or 'a' to abort: ", file_count);
  menu_service_flush(ctx);

  while (1)
  {
    menu_service_getchar(ctx, &key, RX_TIMEOUT);
    if (key == 'a' || key == 'A')
    {
      menu_service_println(ctx, "\rAborted by user.");
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
  menu_service_println(ctx, msg);

  if (storage->path_prefix[0] != '\0')
  {
    snprintf(data_path, sizeof(data_path), "%s%s", storage->path_prefix, file_list[selected - 1]);
  }
  else
  {
    strncpy(data_path, file_list[selected - 1], sizeof(data_path) - 1);
    data_path[sizeof(data_path) - 1] = '\0';
  }

  strncpy(sig_path, data_path, sizeof(sig_path) - 1);
  sig_path[sizeof(sig_path) - 1] = '\0';

  char *dot_pos = strrchr(sig_path, '.');
  if (dot_pos != NULL)
    snprintf(dot_pos, sizeof(sig_path) - (dot_pos - sig_path), ".sig");
  else
    strncat(sig_path, ".sig", sizeof(sig_path) - strlen(sig_path) - 1);

  snprintf(msg, sizeof(msg), "Data file: %s", data_path);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Signature file: %s", sig_path);
  menu_service_println(ctx, msg);
  menu_service_println(ctx, "Starting Ed25519 signature verification...");

  verify_result = ed25519_verify_file(storage->fs, data_path, sig_path, &verify_config);

  if (verify_result == ED25519_VERIFY_OK)
  {
    menu_service_println(ctx, "Ed25519 signature verification PASSED!");
    menu_service_println(ctx, "Firmware integrity confirmed.");
  }
  else
  {
    menu_service_print(ctx, "Ed25519 signature verification FAILED! ");
    menu_service_println(ctx, ed25519_verify_err_to_string(verify_result));
  }
}

static void cmd_ed25519_verify_sdcard(menu_ctx_t *ctx, int argc, char *argv[])
{
  FRESULT res;
  char msg[256];
  ed25519_storage_ctx_t storage = {"SD card", "0:/", "0:/", NULL};

  menu_service_println(ctx, "Initializing TF card...");
  res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
  if (res != FR_OK)
  {
    menu_service_print(ctx, "Error: SD card mount failed! Error code: ");
    menu_service_int2str((uint8_t *)msg, res);
    menu_service_println(ctx, msg);
    return;
  }

  platform_fs_fatfs_register(&g_fs_fatfs, &SDFatFS, "fatfs");
  storage.fs = &g_fs_fatfs.base;

  cmd_ed25519_verify_workflow(ctx, &storage);

  f_mount(NULL, (TCHAR const *)SDPath, 0);
}

static void cmd_ed25519_verify_spi(menu_ctx_t *ctx, int argc, char *argv[])
{
  int res;
  lfs_t lfs;
  ed25519_storage_ctx_t storage = {"SPI Flash", "/", "", NULL};

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

  platform_fs_lfs_register(&g_fs_lfs, &lfs, "lfs");
  storage.fs = &g_fs_lfs.base;

  cmd_ed25519_verify_workflow(ctx, &storage);

  lfs_spi_flash_unmount(&lfs);
}

static void cmd_ed25519_verify_buffer_test(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint8_t test_message[] = "Hello, Ed25519!";
  uint8_t test_public_key[EDSIGN_PUBLIC_KEY_SIZE] = {0};
  uint8_t test_secret_key[EDSIGN_SECRET_KEY_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                     0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
                                                     0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                                     0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
  uint8_t test_signature[EDSIGN_SIGNATURE_SIZE];
  int verify_result;
  char msg[128];

  menu_service_println(ctx, "Running Ed25519 buffer verify test...");

  edsign_sec_to_pub(test_public_key, test_secret_key);
  edsign_sign(test_signature, test_public_key, test_secret_key,
              test_message, sizeof(test_message) - 1);

  menu_service_println(ctx, "Test message: \"Hello, Ed25519!\"");
  snprintf(msg, sizeof(msg), "Public key: %02X%02X%02X%02X...",
           test_public_key[0], test_public_key[1],
           test_public_key[2], test_public_key[3]);
  menu_service_println(ctx, msg);
  snprintf(msg, sizeof(msg), "Signature: %02X%02X%02X%02X...%02X%02X%02X%02X",
           test_signature[0], test_signature[1],
           test_signature[2], test_signature[3],
           test_signature[60], test_signature[61],
           test_signature[62], test_signature[63]);
  menu_service_println(ctx, msg);

  verify_result = ed25519_verify_buffer(test_message, sizeof(test_message) - 1,
                                        test_signature, test_public_key);

  if (verify_result == ED25519_VERIFY_OK)
    menu_service_println(ctx, "Buffer verify test PASSED!");
  else
  {
    menu_service_print(ctx, "Buffer verify test FAILED! ");
    menu_service_println(ctx, ed25519_verify_err_to_string(verify_result));
  }

  test_signature[0] ^= 0xFF;
  verify_result = ed25519_verify_buffer(test_message, sizeof(test_message) - 1,
                                        test_signature, test_public_key);

  if (verify_result == ED25519_VERIFY_ERR_FAILED)
    menu_service_println(ctx, "Tampered signature test PASSED (correctly rejected)!");
  else
    menu_service_println(ctx, "Tampered signature test FAILED (should have been rejected)!");
}
#endif

#if MENU_ENABLE_ESP8266_WIFI
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
#endif

#if MENU_ENABLE_SPI_FLASH_STORE
MENU_TABLE(store_menu) = {
    MENU_ITEM_CMD("1", "Store image from TF card", "Copy from SD to SPI Flash", cmd_store_from_tf),
    MENU_ITEM_CMD("2", "Store image from Flash", "Copy from internal Flash to SPI Flash", cmd_store_from_flash),
    MENU_ITEM_CMD("3", "Show stored images", "List images in SPI Flash", cmd_show_stored),
    MENU_ITEM_CMD("4", "Delete stored image", "Remove image from SPI Flash", cmd_delete_stored),
    MENU_ITEM_CMD("5", "Delete entire file system", "Format SPI Flash", cmd_delete_fs),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_DOWNLOAD
MENU_TABLE(download_menu) = {
    MENU_ITEM_CMD("1", "Download via Serial (Ymodem)", "Receive firmware via Ymodem", cmd_serial_download),
    MENU_ITEM_CMD("2", "Download from SD card (FATFS)", "Read firmware from SD card", cmd_sdcard_download),
    MENU_ITEM_CMD("3", "Download from SPI Flash (LittleFS)", "Read firmware from SPI Flash", cmd_spi_flash_download),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_AES_DECRYPT
MENU_TABLE(decrypt_menu) = {
    MENU_ITEM_CMD("1", "Decrypt from SD card and download to Flash", "Decrypt .bin.aes and write to Flash", cmd_decrypt_download_sdcard),
    MENU_ITEM_CMD("2", "Decrypt from SPI Flash and download to Flash", "Decrypt .bin.aes and write to Flash", cmd_decrypt_download_spi),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_HPATCH
MENU_TABLE(hpatch_menu) = {
    MENU_ITEM_CMD("1", "HPatch upgrade from SD card", "Apply .hdiff patch from SD card", cmd_hpatch_sdcard),
    MENU_ITEM_CMD("2", "HPatch upgrade from SPI Flash", "Apply .hdiff patch from SPI Flash", cmd_hpatch_spi),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_ED25519_VERIFY
MENU_TABLE(ed25519_menu) = {
    MENU_ITEM_CMD("1", "Verify firmware on SD card", "Verify .bin signature from SD card", cmd_ed25519_verify_sdcard),
    MENU_ITEM_CMD("2", "Verify firmware on SPI Flash", "Verify .bin signature from SPI Flash", cmd_ed25519_verify_spi),
    MENU_ITEM_CMD("3", "Buffer verify test", "Self-test Ed25519 sign and verify", cmd_ed25519_verify_buffer_test),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_FIRMWARE_PACKAGE
MENU_TABLE(fw_pkg_menu) = {
    MENU_ITEM_CMD("1", "Parse package from SD card (FATFS)", "Parse .iap.bin and save decrypted to SD", cmd_fw_pkg_sdcard),
    MENU_ITEM_CMD("2", "Parse package from SPI Flash (LFS)", "Parse .iap.bin and save decrypted to SPI", cmd_fw_pkg_spi),
    MENU_ITEM_BACK(),
    MENU_TABLE_END};
#endif

#if MENU_ENABLE_RNG_DEVKEY
static void cmd_rng_generate_devkey(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint32_t random_values[4];
  uint8_t devkey[16];
  HAL_StatusTypeDef status;
  int i;

  menu_service_println(ctx, "Generating 128-bit device key from RNG...");

  for (i = 0; i < 4; i++)
  {
    status = HAL_RNG_GenerateRandomNumber(&hrng, &random_values[i]);
    if (status != HAL_OK)
    {
      menu_service_print(ctx, "Error: RNG generation failed at step ");
      char err_msg[32];
      snprintf(err_msg, sizeof(err_msg), "%d, error code: %d", i + 1, status);
      menu_service_println(ctx, err_msg);
      return;
    }
  }

  for (i = 0; i < 4; i++)
  {
    devkey[i * 4 + 0] = (random_values[i] >> 24) & 0xFF;
    devkey[i * 4 + 1] = (random_values[i] >> 16) & 0xFF;
    devkey[i * 4 + 2] = (random_values[i] >> 8) & 0xFF;
    devkey[i * 4 + 3] = (random_values[i] >> 0) & 0xFF;
  }

  menu_service_println(ctx, "Device Key (128-bit):");
  static char hex_str[64];
  for (i = 0; i < 16; i++)
  {
    snprintf(&hex_str[i * 2], 3, "%02X", devkey[i]);
  }
  hex_str[32] = '\0';
  menu_service_println(ctx, hex_str);

  menu_service_println(ctx, "Formatted:");
  static char formatted_str[64];
  int pos = 0;
  for (i = 0; i < 16; i++)
  {
    snprintf(&formatted_str[pos], 4, "%02X ", devkey[i]);
    pos += 3;
    if ((i + 1) % 4 == 0 && i < 15)
    {
      formatted_str[pos++] = ' ';
    }
  }
  formatted_str[pos] = '\0';
  menu_service_println(ctx, formatted_str);

  menu_service_println(ctx, "Raw values (hex):");
  for (i = 0; i < 4; i++)
  {
    char line[32];
    snprintf(line, sizeof(line), "  R%d: 0x%08lX", i + 1, (unsigned long)random_values[i]);
    menu_service_println(ctx, line);
  }

  menu_service_println(ctx, "");
  menu_service_println(ctx, "=== Chip Unique Device ID (96-bit) ===");
  uint32_t uid_words[3];
  uid_words[0] = *(volatile uint32_t *)(UID_BASE);
  uid_words[1] = *(volatile uint32_t *)(UID_BASE + 4);
  uid_words[2] = *(volatile uint32_t *)(UID_BASE + 8);

  static char uid_uuid_str[40];
  snprintf(uid_uuid_str, sizeof(uid_uuid_str), "%08lX-%08lX-%08lX",
           (unsigned long)uid_words[2], (unsigned long)uid_words[1], (unsigned long)uid_words[0]);
  menu_service_print(ctx, "Device UID: ");
  menu_service_println(ctx, uid_uuid_str);

  static char uid_hex_str[32];
  uint8_t *uid_bytes = (uint8_t *)uid_words;
  for (i = 0; i < 12; i++)
  {
    snprintf(&uid_hex_str[i * 2], 3, "%02X", uid_bytes[i]);
  }
  uid_hex_str[24] = '\0';
  menu_service_print(ctx, "UID (bytes): ");
  menu_service_println(ctx, uid_hex_str);

  menu_service_println(ctx, "");
  menu_service_println(ctx, "=== OTP Memory (528 bytes) ===");

  menu_service_println(ctx, "--- Device Key (16 bytes) ---");
  uint32_t devkey_words[4];
  uint8_t *devkey_bytes = (uint8_t *)devkey_words;
  for (i = 0; i < 4; i++)
  {
    devkey_words[i] = *(volatile uint32_t *)(FLASH_OTP_BASE + i * 4);
  }

  static char devkey_hex[40];
  for (i = 0; i < 16; i++)
  {
    snprintf(&devkey_hex[i * 2], 3, "%02X", devkey_bytes[i]);
  }
  devkey_hex[32] = '\0';
  menu_service_print(ctx, "DevKey: ");
  menu_service_println(ctx, devkey_hex);

  static char devkey_formatted[64];
  int dk_pos = 0;
  for (i = 0; i < 16; i++)
  {
    snprintf(&devkey_formatted[dk_pos], 4, "%02X ", devkey_bytes[i]);
    dk_pos += 3;
    if ((i + 1) % 4 == 0 && i < 15)
    {
      devkey_formatted[dk_pos++] = ' ';
    }
  }
  devkey_formatted[dk_pos] = '\0';
  menu_service_println(ctx, devkey_formatted);

  menu_service_println(ctx, "");
  menu_service_println(ctx, "--- User Data (512 bytes) ---");
  volatile uint8_t *otp_user = (volatile uint8_t *)(FLASH_OTP_BASE + 16);
  static char otp_line[64];
  int row, col;
  for (row = 0; row < 32; row++)
  {
    int line_pos = 0;
    snprintf(otp_line, sizeof(otp_line), "%03X: ", 16 + row * 16);
    line_pos = 5;
    for (col = 0; col < 16; col++)
    {
      snprintf(&otp_line[line_pos], 4, "%02X ", otp_user[row * 16 + col]);
      line_pos += 3;
    }
    otp_line[line_pos] = '\0';
    menu_service_println(ctx, otp_line);
  }
}

static void cmd_write_otp_devkey(menu_ctx_t *ctx, int argc, char *argv[])
{
  uint32_t random_values[4];
  uint8_t devkey[16];
  HAL_StatusTypeDef status;
  int i;

  menu_service_println(ctx, "Checking OTP status...");
  uint8_t otp_empty = 1;
  for (i = 0; i < 4; i++)
  {
    if (*(volatile uint32_t *)(FLASH_OTP_BASE + i * 4) != 0xFFFFFFFF)
    {
      otp_empty = 0;
      break;
    }
  }

  if (!otp_empty)
  {
    menu_service_println(ctx, "Error: OTP already programmed!");
    menu_service_println(ctx, "OTP can only be written once.");
    uint32_t existing_words[4];
    uint8_t *existing_bytes = (uint8_t *)existing_words;
    for (i = 0; i < 4; i++)
    {
      existing_words[i] = *(volatile uint32_t *)(FLASH_OTP_BASE + i * 4);
    }
    static char existing_hex[40];
    for (i = 0; i < 16; i++)
    {
      snprintf(&existing_hex[i * 2], 3, "%02X", existing_bytes[i]);
    }
    existing_hex[32] = '\0';
    menu_service_print(ctx, "Current OTP: ");
    menu_service_println(ctx, existing_hex);
    return;
  }

  menu_service_println(ctx, "Generating 128-bit device key...");
  for (i = 0; i < 4; i++)
  {
    status = HAL_RNG_GenerateRandomNumber(&hrng, &random_values[i]);
    if (status != HAL_OK)
    {
      menu_service_println(ctx, "Error: RNG generation failed!");
      return;
    }
  }

  for (i = 0; i < 4; i++)
  {
    devkey[i * 4 + 0] = (random_values[i] >> 24) & 0xFF;
    devkey[i * 4 + 1] = (random_values[i] >> 16) & 0xFF;
    devkey[i * 4 + 2] = (random_values[i] >> 8) & 0xFF;
    devkey[i * 4 + 3] = (random_values[i] >> 0) & 0xFF;
  }

  static char key_hex[40];
  for (i = 0; i < 16; i++)
  {
    snprintf(&key_hex[i * 2], 3, "%02X", devkey[i]);
  }
  key_hex[32] = '\0';
  menu_service_print(ctx, "Device Key: ");
  menu_service_println(ctx, key_hex);

  menu_service_println(ctx, "Writing to OTP...");
  HAL_FLASH_Unlock();

  status = HAL_OK;
  for (i = 0; i < 4; i++)
  {
    uint32_t address = FLASH_OTP_BASE + (i * 4);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, random_values[i]) != HAL_OK)
    {
      status = HAL_ERROR;
      break;
    }
  }

  HAL_FLASH_Lock();

  if (status != HAL_OK)
  {
    menu_service_println(ctx, "Error: OTP write failed!");
    return;
  }

  menu_service_println(ctx, "OTP write successful!");
  menu_service_println(ctx, "");
  menu_service_println(ctx, "Verifying...");

  uint32_t otp_words[4];
  for (i = 0; i < 4; i++)
  {
    otp_words[i] = *(volatile uint32_t *)(FLASH_OTP_BASE + i * 4);
  }

  static char verify_hex[40];
  uint8_t *otp_bytes = (uint8_t *)otp_words;
  for (i = 0; i < 16; i++)
  {
    snprintf(&verify_hex[i * 2], 3, "%02X", otp_bytes[i]);
  }
  verify_hex[32] = '\0';
  menu_service_print(ctx, "OTP: ");
  menu_service_println(ctx, verify_hex);

  int verify_ok = 1;
  for (i = 0; i < 4; i++)
  {
    if (otp_words[i] != random_values[i])
    {
      verify_ok = 0;
      break;
    }
  }

  if (verify_ok)
  {
    menu_service_println(ctx, "Verification: PASSED");
  }
  else
  {
    menu_service_println(ctx, "Verification: FAILED!");
  }
}
#endif

MENU_TABLE(main_menu) = {
#if MENU_ENABLE_DOWNLOAD
    MENU_ITEM_SUBMENU("1", "Download image to internal Flash", "Firmware download options", download_menu, sizeof(download_menu) / sizeof(download_menu[0]) - 1),
#endif
#if MENU_ENABLE_UPLOAD
    MENU_ITEM_CMD("2", "Upload image from internal Flash", "Send firmware via Ymodem", cmd_serial_upload),
#endif
#if MENU_ENABLE_SPI_FLASH_STORE
    MENU_ITEM_SUBMENU("3", "Store image to SPI-Flash LFS", "SPI Flash storage management", store_menu, sizeof(store_menu) / sizeof(store_menu[0]) - 1),
#endif
#if MENU_ENABLE_EXECUTE_APP
    MENU_ITEM_CMD("4", "Execute the loaded application", "Jump to application firmware", cmd_execute_app),
#endif
#if MENU_ENABLE_FLASH_PROTECTION
    MENU_ITEM_CMD("5", "Toggle Flash write protection", "Enable/disable write protection", cmd_flash_protection),
#endif
#if MENU_ENABLE_AES_DECRYPT
    MENU_ITEM_SUBMENU("6", "Decrypt and download encrypted firmware", "AES decryption options", decrypt_menu, sizeof(decrypt_menu) / sizeof(decrypt_menu[0]) - 1),
    MENU_ITEM_CMD("7", "Decrypt .bin.aes file on SD card", "Decrypt and save to SD card", cmd_decrypt_sdcard),
#endif
#if MENU_ENABLE_HPATCH
    MENU_ITEM_SUBMENU("8", "HPatch differential upgrade", "Differential firmware update", hpatch_menu, sizeof(hpatch_menu) / sizeof(hpatch_menu[0]) - 1),
#endif
#if MENU_ENABLE_UART_PASSTHROUGH
    MENU_ITEM_CMD("9", "UART4 <-> USART1 Passthrough", "Transparent UART bridge", cmd_uart_passthrough),
#endif
#if MENU_ENABLE_ESP8266_WIFI
    MENU_ITEM_SUBMENU("A", "ESP8266 WiFi & OTA Test", "WiFi and OTA testing", esp8266_menu, sizeof(esp8266_menu) / sizeof(esp8266_menu[0]) - 1),
#endif
#if MENU_ENABLE_ED25519_VERIFY
    MENU_ITEM_SUBMENU("B", "Ed25519 Signature Verify", "Firmware signature verification", ed25519_menu, sizeof(ed25519_menu) / sizeof(ed25519_menu[0]) - 1),
#endif
#if MENU_ENABLE_RNG_DEVKEY
    MENU_ITEM_CMD("C", "Generate Device Key (RNG)", "Generate 128-bit random device key", cmd_rng_generate_devkey),
    MENU_ITEM_CMD("D", "Write DevKey to OTP", "Generate and write 128-bit key to OTP", cmd_write_otp_devkey),
#endif
#if MENU_ENABLE_FIRMWARE_PACKAGE
    MENU_ITEM_SUBMENU("E", "Firmware Package Parse", "Parse .iap.bin package and decrypt", fw_pkg_menu, sizeof(fw_pkg_menu) / sizeof(fw_pkg_menu[0]) - 1),
#endif
    MENU_TABLE_END};

void Main_Menu(void)
{
  menu_service_init(&g_menu_ctx, (platform_uart_base_t *)&g_uart4_console.base);
  menu_service_set_root(&g_menu_ctx, main_menu, sizeof(main_menu) / sizeof(main_menu[0]) - 1);
  menu_service_set_prompt(&g_menu_ctx, "STM32F4xx IAP Menu");

  menu_service_run(&g_menu_ctx);
}

/**
 * @}
 */
