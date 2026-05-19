/**
 ******************************************************************************
 * @file    IAP/IAP_Main/Src/ymodem.c
 * @author  MCD Application Team
 * @brief   This file provides all the software functions related to the ymodem
 *          protocol.
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

#include "ymodem.h"
#include "service_ymodem.h"
#include "platform_config.h"
#include "bootloader_core.h"
#include "ab_partition.h"
#include "main.h"
#include <string.h>

#define USER_FLASH_SIZE (g_internal_flash.flash_base.total_size)

static ymodem_config_t g_ymodem_config;
static ymodem_ctx_t g_ymodem_ctx;
static uint8_t g_file_name[FILE_NAME_LENGTH];
extern uint8_t aFileName[FILE_NAME_LENGTH];

COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size)
{
  g_ymodem_config.uart = (platform_uart_base_t *)&g_uart4_console.base;
  g_ymodem_config.transport = (platform_transport_base_t *)&g_internal_flash.transport_base;
  g_ymodem_config.max_size = USER_FLASH_SIZE;
  g_ymodem_config.file_name = aFileName;
  g_ymodem_config.file_name_len = FILE_NAME_LENGTH;

  ymodem_status_t status = ymodem_service_receive(&g_ymodem_config, &g_ymodem_ctx, p_size);

  return (COM_StatusTypeDef)status;
}

COM_StatusTypeDef Ymodem_Receive_To_Slot(uint32_t *p_size, ab_slot_t slot)
{
  internal_flash_stm32_t *target_flash;

  if (slot == AB_SLOT_A)
    target_flash = &g_slot_a_flash;
  else if (slot == AB_SLOT_B)
    target_flash = &g_slot_b_flash;
  else
    return COM_ERROR;

  g_ymodem_config.uart = (platform_uart_base_t *)&g_uart4_console.base;
  g_ymodem_config.transport = (platform_transport_base_t *)&target_flash->transport_base;
  g_ymodem_config.max_size = ab_partition_get_slot_size(slot);
  g_ymodem_config.file_name = aFileName;
  g_ymodem_config.file_name_len = FILE_NAME_LENGTH;

  ymodem_status_t status = ymodem_service_receive(&g_ymodem_config, &g_ymodem_ctx, p_size);

  if ((COM_StatusTypeDef)status == COM_OK)
  {
    ab_partition_update_slot_meta(slot, 0, 0, 0, NULL);
    ab_partition_set_active_slot(slot);
  }

  return (COM_StatusTypeDef)status;
}

COM_StatusTypeDef Ymodem_Transmit(uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
  g_ymodem_config.uart = (platform_uart_base_t *)&g_uart4_console.base;
  g_ymodem_config.transport = (platform_transport_base_t *)&g_internal_flash.transport_base;
  g_ymodem_config.max_size = USER_FLASH_SIZE;
  g_ymodem_config.file_name = g_file_name;
  g_ymodem_config.file_name_len = FILE_NAME_LENGTH;

  ymodem_status_t status = ymodem_service_transmit(&g_ymodem_config, &g_ymodem_ctx, p_buf, p_file_name, file_size);

  return (COM_StatusTypeDef)status;
}

/**
 * @}
 */

/*******************(C)COPYRIGHT 2016 STMicroelectronics *****END OF FILE****/
