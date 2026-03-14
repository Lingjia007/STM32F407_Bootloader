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
#include "stdio.h"
#include "string.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define MAX_FILES 20
#define MAX_FILENAME_LEN 64

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
pFunction JumpToApplication;
uint32_t JumpAddress;
uint32_t FlashProtection = 0;
uint8_t aFileName[FILE_NAME_LENGTH];

static char file_list[MAX_FILES][MAX_FILENAME_LEN];
static uint8_t file_count = 0;

/* Private function prototypes -----------------------------------------------*/
void SerialDownload(void);
void SerialUpload(void);
void SDCardDownload(void);

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

  if (len >= 7)
  {
    ext = filename + len - 7;
    if (strcmp(ext, ".bin.aes") == 0 || strcmp(ext, ".BIN.AES") == 0)
    {
      return 1;
    }
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

void SDCardDownload(void)
{
  uint8_t key = 0;
  uint8_t selected = 0;
  uint8_t i;
  char msg[128];
  FRESULT res;
  bootloader_err_t err;
  fatfs_src_priv_t src_priv;
  internal_flash_target_priv_t tgt_priv;

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

  Serial_PutString((uint8_t *)"\r\nPlease select a file (1-");
  msg[0] = '0' + file_count;
  msg[1] = ')';
  msg[2] = ' ';
  msg[3] = 'o';
  msg[4] = 'r';
  msg[5] = ' ';
  msg[6] = 'p';
  msg[7] = 'r';
  msg[8] = 'e';
  msg[9] = 's';
  msg[10] = 's';
  msg[11] = ' ';
  msg[12] = '\'';
  msg[13] = 'a';
  msg[14] = '\'';
  msg[15] = ' ';
  msg[16] = 't';
  msg[17] = 'o';
  msg[18] = ' ';
  msg[19] = 'a';
  msg[20] = 'b';
  msg[21] = 'o';
  msg[22] = 'r';
  msg[23] = 't';
  msg[24] = ':';
  msg[25] = ' ';
  msg[26] = '\0';
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

  src_priv.fs = &SDFatFS;
  strncpy(src_priv.path, file_list[selected - 1], sizeof(src_priv.path) - 1);
  src_priv.path[sizeof(src_priv.path) - 1] = '\0';

  tgt_priv.start_addr = APPLICATION_ADDRESS;

  err = bootloader_download(&fatfs_source_if, &src_priv,
                            &internal_flash_target_if, &tgt_priv,
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
    Serial_PutString((uint8_t *)"  Download image to the internal Flash ----------------- 1\r\n\n");
    Serial_PutString((uint8_t *)"  Upload image from the internal Flash ----------------- 2\r\n\n");
    Serial_PutString((uint8_t *)"  Execute the loaded application ----------------------- 3\r\n\n");
    Serial_PutString((uint8_t *)"  Download image from SD card to the internal Flash ---- 4\r\n\n");

    if (FlashProtection != FLASHIF_PROTECTION_NONE)
    {
      Serial_PutString((uint8_t *)"  Disable the write protection ------------------------- 5\r\n\n");
    }
    else
    {
      Serial_PutString((uint8_t *)"  Enable the write protection -------------------------- 5\r\n\n");
    }
    Serial_PutString((uint8_t *)"==========================================================\r\n\n");

    /* Clean the input path */
    __HAL_UART_FLUSH_DRREGISTER(&UartHandle);

    /* Receive key */
    HAL_UART_Receive(&UartHandle, &key, 1, RX_TIMEOUT);

    switch (key)
    {
    case '1':
      /* Download user application in the Flash */
      SerialDownload();
      break;
    case '2':
      /* Upload user application from the Flash */
      SerialUpload();
      break;
    case '3':
      Serial_PutString((uint8_t *)"Start program execution......\r\n\n");
      bootloader_ctx.jump_func(bootloader_ctx.app_jump_addr);
      break;
    case '4':
      SDCardDownload();
      break;
    case '5':
      if (FlashProtection != FLASHIF_PROTECTION_NONE)
      {
        /* Disable the write protection */
        if (FLASH_If_WriteProtectionConfig(OB_WRPSTATE_DISABLE) == HAL_OK)
        {
          Serial_PutString((uint8_t *)"Write Protection disabled...\r\n");
          Serial_PutString((uint8_t *)"System will now restart...\r\n");
          /* Launch the option byte loading */
          HAL_FLASH_OB_Launch();
          /* Ulock the flash */
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
          /* Launch the option byte loading */
          HAL_FLASH_OB_Launch();
        }
        else
        {
          Serial_PutString((uint8_t *)"Error: Flash write protection failed...\r\n");
        }
      }
      break;
    default:
      Serial_PutString((uint8_t *)"Invalid Number ! ==> The number should be either 1, 2, 3, 4 or 5\r");
      break;
    }
  }
}

/**
 * @}
 */
