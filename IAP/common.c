/**
 ******************************************************************************
 * @file    IAP/IAP_Main/Src/common.c
 * @author  MCD Application Team
 * @brief   This file provides all the common functions.
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
#include "common.h"
#include "main.h"
#include "usart.h"
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
UART_HandleTypeDef UartHandle = {0};
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Initialize UART handle for IAP communication
 * @param  None
 * @retval None
 */
void Common_Init(Ymodem_serial_params_t *params)
{
  /* 检查参数有效性 */
  if (params == NULL)
  {
    return;
  }

  /* 设置UART句柄 */
  params->huart = &huart4;

  /* 设置默认下载地址 */
  params->dest_addr = bootloader_ctx.app_jump_addr;

  /* 配置UartHandle参数 */
  UartHandle = *(UART_HandleTypeDef *)bootloader_ctx.serial_params.huart;
}

/**
 * @brief  Convert an Integer to a string
 * @param  p_str: The string output pointer
 * @param  intnum: The integer to be converted
 * @retval None
 */
void Int2Str(uint8_t *p_str, uint32_t intnum)
{
  uint32_t i, divider = 1000000000, pos = 0, status = 0;

  for (i = 0; i < 10; i++)
  {
    p_str[pos++] = (intnum / divider) + 48;

    intnum = intnum % divider;
    divider /= 10;
    if ((p_str[pos - 1] == '0') & (status == 0))
    {
      pos = 0;
    }
    else
    {
      status++;
    }
  }
}

/**
 * @brief  Convert a string to an integer
 * @param  p_inputstr: The string to be converted
 * @param  p_intnum: The integer value
 * @retval 1: Correct
 *         0: Error
 */
uint32_t Str2Int(uint8_t *p_inputstr, uint32_t *p_intnum)
{
  uint32_t i = 0, res = 0;
  uint32_t val = 0;

  if ((p_inputstr[0] == '0') && ((p_inputstr[1] == 'x') || (p_inputstr[1] == 'X')))
  {
    i = 2;
    while ((i < 11) && (p_inputstr[i] != '\0'))
    {
      if (ISVALIDHEX(p_inputstr[i]))
      {
        val = (val << 4) + CONVERTHEX(p_inputstr[i]);
      }
      else
      {
        /* Return 0, Invalid input */
        res = 0;
        break;
      }
      i++;
    }

    /* valid result */
    if (p_inputstr[i] == '\0')
    {
      *p_intnum = val;
      res = 1;
    }
  }
  else /* max 10-digit decimal input */
  {
    while ((i < 11) && (res != 1))
    {
      if (p_inputstr[i] == '\0')
      {
        *p_intnum = val;
        /* return 1 */
        res = 1;
      }
      else if (((p_inputstr[i] == 'k') || (p_inputstr[i] == 'K')) && (i > 0))
      {
        val = val << 10;
        *p_intnum = val;
        res = 1;
      }
      else if (((p_inputstr[i] == 'm') || (p_inputstr[i] == 'M')) && (i > 0))
      {
        val = val << 20;
        *p_intnum = val;
        res = 1;
      }
      else if (ISVALIDDEC(p_inputstr[i]))
      {
        val = val * 10 + CONVERTDEC(p_inputstr[i]);
      }
      else
      {
        /* return 0, Invalid input */
        res = 0;
        break;
      }
      i++;
    }
  }

  return res;
}

/**
 * @brief  Print a string on the HyperTerminal
 * @param  p_string: The string to be printed
 * @retval None
 */
void Serial_PutString(uint8_t *p_string)
{
  uint16_t length = 0;

  while (p_string[length] != '\0')
  {
    length++;
  }

  // 使用 bootloader_ctx 中的 UART 句柄
  HAL_UART_Transmit((UART_HandleTypeDef *)bootloader_ctx.serial_params.huart, p_string, length, TX_TIMEOUT);
}

/**
 * @brief  Transmit a byte to the HyperTerminal
 * @param  param The byte to be sent
 * @retval HAL_StatusTypeDef HAL_OK if OK
 */
HAL_StatusTypeDef Serial_PutByte(uint8_t param)
{
  // 获取 UART 句柄指针
  UART_HandleTypeDef *huart = (UART_HandleTypeDef *)bootloader_ctx.serial_params.huart;

  /* May be timeouted... */
  if (huart->gState == HAL_UART_STATE_TIMEOUT)
  {
    huart->gState = HAL_UART_STATE_READY;
  }

  return HAL_UART_Transmit(huart, &param, 1, TX_TIMEOUT);
}
/**
 * @}
 */
