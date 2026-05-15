/**
 ******************************************************************************
 * @file    IAP/IAP_Main/Inc/menu.h
 * @author  MCD Application Team
 * @brief   This file provides all the headers of the menu functions.
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MENU_H
#define __MENU_H

/* Includes ------------------------------------------------------------------*/
#include "ymodem.h"
#include "Bootloader_core.h"
#include "platform_config.h"

/* Menu Feature Configuration Macros ------------------------------------------*/
#ifndef MENU_ENABLE_DOWNLOAD
#define MENU_ENABLE_DOWNLOAD 1
#endif

#ifndef MENU_ENABLE_UPLOAD
#define MENU_ENABLE_UPLOAD 0
#endif

#ifndef MENU_ENABLE_SPI_FLASH_STORE
#define MENU_ENABLE_SPI_FLASH_STORE 1
#endif

#ifndef MENU_ENABLE_EXECUTE_APP
#define MENU_ENABLE_EXECUTE_APP 1
#endif

#ifndef MENU_ENABLE_FLASH_PROTECTION
#define MENU_ENABLE_FLASH_PROTECTION 0
#endif

#ifndef MENU_ENABLE_AES_DECRYPT
#define MENU_ENABLE_AES_DECRYPT 1
#endif

#ifndef MENU_ENABLE_HPATCH
#define MENU_ENABLE_HPATCH 1
#endif

#ifndef MENU_ENABLE_UART_PASSTHROUGH
#define MENU_ENABLE_UART_PASSTHROUGH 0
#endif

#ifndef MENU_ENABLE_ESP8266_WIFI
#define MENU_ENABLE_ESP8266_WIFI 0
#endif

#ifndef MENU_ENABLE_ED25519_VERIFY
#define MENU_ENABLE_ED25519_VERIFY 1
#endif

#ifndef MENU_ENABLE_RNG_DEVKEY
#define MENU_ENABLE_RNG_DEVKEY 1
#endif

#ifndef MENU_ENABLE_FIRMWARE_PACKAGE
#define MENU_ENABLE_FIRMWARE_PACKAGE 1
#endif

/* Imported variables --------------------------------------------------------*/
extern uint8_t aFileName[FILE_NAME_LENGTH];

/* Private variables ---------------------------------------------------------*/
/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
void Main_Menu(void);

#endif /* __MENU_H */
