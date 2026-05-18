/**
  ******************************************************************************
  * @file    IAP/IAP_Main/Inc/ymodem.h 
  * @author  MCD Application Team
  * @brief   This file provides all the software function headers of the ymodem.c 
  *          file.
  ******************************************************************************
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */ 

#ifndef __YMODEM_H_
#define __YMODEM_H_

#include "service_ymodem.h"
#include "ab_partition.h"

typedef enum
{
  COM_OK       = YMODEM_OK,
  COM_ERROR    = YMODEM_ERROR,
  COM_ABORT    = YMODEM_ABORT,
  COM_TIMEOUT  = YMODEM_TIMEOUT,
  COM_DATA     = YMODEM_DATA,
  COM_LIMIT    = YMODEM_LIMIT
} COM_StatusTypeDef;

COM_StatusTypeDef Ymodem_Receive(uint32_t *p_size);
COM_StatusTypeDef Ymodem_Receive_To_Slot(uint32_t *p_size, ab_slot_t slot);
COM_StatusTypeDef Ymodem_Transmit(uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size);

#endif

/*******************(C)COPYRIGHT STMicroelectronics ********END OF FILE********/
