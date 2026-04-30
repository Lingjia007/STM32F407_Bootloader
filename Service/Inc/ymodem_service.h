#ifndef YMODEM_SERVICE_H
#define YMODEM_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include "platform_transport.h"
#include "platform_uart.h"

#define PACKET_HEADER_SIZE ((uint32_t)3)
#define PACKET_DATA_INDEX ((uint32_t)4)
#define PACKET_START_INDEX ((uint32_t)1)
#define PACKET_NUMBER_INDEX ((uint32_t)2)
#define PACKET_CNUMBER_INDEX ((uint32_t)3)
#define PACKET_TRAILER_SIZE ((uint32_t)2)
#define PACKET_OVERHEAD_SIZE (PACKET_HEADER_SIZE + PACKET_TRAILER_SIZE - 1)
#define PACKET_SIZE ((uint32_t)128)
#define PACKET_1K_SIZE ((uint32_t)1024)

#define FILE_NAME_LENGTH ((uint32_t)64)
#define FILE_SIZE_LENGTH ((uint32_t)16)

#define SOH ((uint8_t)0x01)
#define STX ((uint8_t)0x02)
#define EOT ((uint8_t)0x04)
#define ACK ((uint8_t)0x06)
#define NAK ((uint8_t)0x15)
#define CA ((uint32_t)0x18)
#define CRC16 ((uint8_t)0x43)
#define NEGATIVE_BYTE ((uint8_t)0xFF)

#define ABORT1 ((uint8_t)0x41)
#define ABORT2 ((uint8_t)0x61)

#define NAK_TIMEOUT ((uint32_t)0x100000)
#define DOWNLOAD_TIMEOUT ((uint32_t)5000)
#define MAX_ERRORS ((uint32_t)5)

typedef enum
{
    YMODEM_OK = 0x00,
    YMODEM_ERROR = 0x01,
    YMODEM_ABORT = 0x02,
    YMODEM_TIMEOUT = 0x03,
    YMODEM_DATA = 0x04,
    YMODEM_LIMIT = 0x05
} ymodem_status_t;

typedef struct
{
    platform_uart_base_t *uart;
    platform_transport_base_t *transport;
    uint32_t max_size;
    uint8_t *file_name;
    uint32_t file_name_len;
} ymodem_config_t;

typedef struct
{
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_DATA_INDEX + PACKET_TRAILER_SIZE];
    uint32_t write_offset;
    uint32_t errors;
    uint32_t packets_received;
    uint32_t total_size;
    uint8_t session_done;
    uint8_t file_done;
    uint8_t session_begin;
    uint8_t transport_opened;
} ymodem_ctx_t;

ymodem_status_t ymodem_service_receive(ymodem_config_t *config, ymodem_ctx_t *ctx, uint32_t *p_size);
ymodem_status_t ymodem_service_transmit(ymodem_config_t *config, ymodem_ctx_t *ctx,
                                        uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size);

uint16_t ymodem_cal_crc16(const uint8_t *p_data, uint32_t size);
uint8_t ymodem_calc_checksum(const uint8_t *p_data, uint32_t size);

#endif
