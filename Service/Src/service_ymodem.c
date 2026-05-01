#include "service_ymodem.h"
#include <string.h>

#define CRC16_F

static uint16_t UpdateCRC16(uint16_t crc_in, uint8_t byte)
{
    uint32_t crc = crc_in;
    uint32_t in = byte | 0x100;

    do
    {
        crc <<= 1;
        in <<= 1;
        if (in & 0x100)
            ++crc;
        if (crc & 0x10000)
            crc ^= 0x1021;
    }

    while (!(in & 0x10000));

    return crc & 0xffffu;
}

uint16_t ymodem_cal_crc16(const uint8_t *p_data, uint32_t size)
{
    uint32_t crc = 0;
    const uint8_t *dataEnd = p_data + size;

    while (p_data < dataEnd)
        crc = UpdateCRC16(crc, *p_data++);

    crc = UpdateCRC16(crc, 0);
    crc = UpdateCRC16(crc, 0);

    return crc & 0xffffu;
}

uint8_t ymodem_calc_checksum(const uint8_t *p_data, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t *p_data_end = p_data + size;

    while (p_data < p_data_end)
    {
        sum += *p_data++;
    }

    return (sum & 0xffu);
}

static void PrepareIntialPacket(uint8_t *p_data, const uint8_t *p_file_name, uint32_t length)
{
    uint32_t i, j = 0;
    uint8_t astring[10];
    uint32_t divider = 1000000000;

    p_data[PACKET_START_INDEX] = SOH;
    p_data[PACKET_NUMBER_INDEX] = 0x00;
    p_data[PACKET_CNUMBER_INDEX] = 0xff;

    for (i = 0; (p_file_name[i] != '\0') && (i < FILE_NAME_LENGTH); i++)
    {
        p_data[i + PACKET_DATA_INDEX] = p_file_name[i];
    }

    p_data[i + PACKET_DATA_INDEX] = 0x00;

    for (i = 0; i < 10; i++)
    {
        astring[i] = (length / divider) + 48;
        length = length % divider;
        divider /= 10;
        if ((astring[i] == '0') && (j == 0))
        {
            i--;
        }
        else
        {
            j++;
        }
    }

    if (j == 0)
    {
        astring[0] = '0';
        j = 1;
    }
    astring[j] = '\0';

    i = i + PACKET_DATA_INDEX + 1;
    for (j = 0; astring[j] != '\0'; j++)
    {
        p_data[i++] = astring[j];
    }

    for (j = i; j < PACKET_SIZE + PACKET_DATA_INDEX; j++)
    {
        p_data[j] = 0;
    }
}

static void PreparePacket(uint8_t *p_source, uint8_t *p_packet, uint8_t pkt_nr, uint32_t size_blk)
{
    uint8_t *p_record;
    uint32_t i, size, packet_size;

    packet_size = size_blk >= PACKET_1K_SIZE ? PACKET_1K_SIZE : PACKET_SIZE;
    size = size_blk < packet_size ? size_blk : packet_size;

    if (packet_size == PACKET_1K_SIZE)
    {
        p_packet[PACKET_START_INDEX] = STX;
    }
    else
    {
        p_packet[PACKET_START_INDEX] = SOH;
    }

    p_packet[PACKET_NUMBER_INDEX] = pkt_nr;
    p_packet[PACKET_CNUMBER_INDEX] = (~pkt_nr);
    p_record = p_source;

    for (i = PACKET_DATA_INDEX; i < size + PACKET_DATA_INDEX; i++)
    {
        p_packet[i] = *p_record++;
    }

    if (size <= packet_size)
    {
        for (i = size + PACKET_DATA_INDEX; i < packet_size + PACKET_DATA_INDEX; i++)
        {
            p_packet[i] = 0x1A;
        }
    }
}

static int16_t ReceivePacket(ymodem_config_t *config, ymodem_ctx_t *ctx, uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
    uint32_t crc;
    uint32_t packet_size = 0;
    int16_t status;
    uint8_t char1;

    (void)ctx;

    *p_length = 0;
    status = UART_RECEIVE(config->uart, &char1, 1, timeout);

    if (status == 0)
    {
        switch (char1)
        {
        case SOH:
            packet_size = PACKET_SIZE;
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;
            break;
        case EOT:
            break;
        case CA:
            if (UART_RECEIVE(config->uart, &char1, 1, timeout) == 0 && (char1 == CA))
            {
                packet_size = 2;
            }
            else
            {
                status = -1;
            }
            break;
        case ABORT1:
        case ABORT2:
            status = -2;
            break;
        default:
            status = -1;
            break;
        }
        *p_data = char1;

        if (packet_size >= PACKET_SIZE)
        {
            status = UART_RECEIVE(config->uart, &p_data[PACKET_NUMBER_INDEX],
                                  packet_size + PACKET_OVERHEAD_SIZE, timeout);

            if (status == 0)
            {
                if (p_data[PACKET_NUMBER_INDEX] != ((p_data[PACKET_CNUMBER_INDEX]) ^ NEGATIVE_BYTE))
                {
                    packet_size = 0;
                    status = -1;
                }
                else
                {
                    crc = p_data[packet_size + PACKET_DATA_INDEX] << 8;
                    crc += p_data[packet_size + PACKET_DATA_INDEX + 1];
                    if (ymodem_cal_crc16(&p_data[PACKET_DATA_INDEX], packet_size) != crc)
                    {
                        packet_size = 0;
                        status = -1;
                    }
                }
            }
            else
            {
                packet_size = 0;
            }
        }
    }
    *p_length = packet_size;
    return status;
}

ymodem_status_t ymodem_service_receive(ymodem_config_t *config, ymodem_ctx_t *ctx, uint32_t *p_size)
{
    uint32_t i, packet_length, filesize;
    uint8_t *file_ptr;
    uint8_t file_size[FILE_SIZE_LENGTH], tmp;
    ymodem_status_t result = YMODEM_OK;

    ctx->write_offset = 0;
    ctx->session_done = 0;
    ctx->errors = 0;
    ctx->session_begin = 0;
    ctx->transport_opened = 0;
    ctx->total_size = 0;

    while ((ctx->session_done == 0) && (result == YMODEM_OK))
    {
        ctx->packets_received = 0;
        ctx->file_done = 0;

        while ((ctx->file_done == 0) && (result == YMODEM_OK))
        {
            int16_t recv_status = ReceivePacket(config, ctx, ctx->packet_data, &packet_length, DOWNLOAD_TIMEOUT);

            if (recv_status == 0)
            {
                ctx->errors = 0;
                switch (packet_length)
                {
                case 2:
                    UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                    result = YMODEM_ABORT;
                    break;
                case 0:
                    UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                    ctx->file_done = 1;
                    break;
                default:
                    if (ctx->packet_data[PACKET_NUMBER_INDEX] != (uint8_t)ctx->packets_received)
                    {
                        UART_TRANSMIT(config->uart, (uint8_t[]){NAK}, 1, 100);
                    }
                    else
                    {
                        if (ctx->packets_received == 0)
                        {
                            if (ctx->packet_data[PACKET_DATA_INDEX] != 0)
                            {
                                i = 0;
                                file_ptr = ctx->packet_data + PACKET_DATA_INDEX;
                                while ((*file_ptr != 0) && (i < FILE_NAME_LENGTH))
                                {
                                    if (config->file_name && i < config->file_name_len - 1)
                                    {
                                        config->file_name[i] = *file_ptr;
                                    }
                                    i++;
                                    file_ptr++;
                                }

                                if (config->file_name && i < config->file_name_len)
                                {
                                    config->file_name[i] = '\0';
                                }

                                file_ptr++;
                                i = 0;
                                while ((*file_ptr != ' ') && (i < FILE_SIZE_LENGTH))
                                {
                                    file_size[i++] = *file_ptr++;
                                }
                                file_size[i] = '\0';

                                filesize = 0;
                                for (i = 0; file_size[i] != '\0'; i++)
                                {
                                    if (file_size[i] >= '0' && file_size[i] <= '9')
                                    {
                                        filesize = filesize * 10 + (file_size[i] - '0');
                                    }
                                }

                                if (filesize > config->max_size)
                                {
                                    tmp = CA;
                                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                    result = YMODEM_LIMIT;
                                }
                                else
                                {
                                    ctx->total_size = filesize;
                                    *p_size = filesize;

                                    if (config->transport)
                                    {
                                        int16_t open_result = TRANSPORT_TARGET_OPEN(config->transport, NULL, filesize);
                                        if (open_result == (int16_t)TRANSPORT_STATUS_OK)
                                        {
                                            ctx->transport_opened = 1;
                                        }
                                        else
                                        {
                                            tmp = CA;
                                            UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                            UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                            result = YMODEM_DATA;
                                            break;
                                        }
                                    }

                                    UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                                    tmp = CRC16;
                                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                }
                            }
                            else
                            {
                                UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                                ctx->file_done = 1;
                                ctx->session_done = 1;
                                break;
                            }
                        }
                        else
                        {
                            if (config->transport && ctx->transport_opened)
                            {
                                int16_t write_status = TRANSPORT_TARGET_WRITE(
                                    config->transport,
                                    ctx->write_offset,
                                    &ctx->packet_data[PACKET_DATA_INDEX],
                                    packet_length);

                                if (write_status == (int16_t)TRANSPORT_STATUS_OK)
                                {
                                    ctx->write_offset += packet_length;
                                    UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                                }
                                else
                                {
                                    tmp = CA;
                                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                                    result = YMODEM_DATA;
                                }
                            }
                            else
                            {
                                ctx->write_offset += packet_length;
                                UART_TRANSMIT(config->uart, (uint8_t[]){ACK}, 1, 100);
                            }
                        }
                        ctx->packets_received++;
                        ctx->session_begin = 1;
                    }
                    break;
                }
            }
            else if (recv_status == -2)
            {
                tmp = CA;
                UART_TRANSMIT(config->uart, &tmp, 1, 100);
                UART_TRANSMIT(config->uart, &tmp, 1, 100);
                result = YMODEM_ABORT;
            }
            else
            {
                if (ctx->session_begin > 0)
                {
                    ctx->errors++;
                }
                if (ctx->errors > MAX_ERRORS)
                {
                    tmp = CA;
                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                }
                else
                {
                    tmp = CRC16;
                    UART_TRANSMIT(config->uart, &tmp, 1, 100);
                }
            }
        }
    }

    if (ctx->transport_opened && config->transport)
    {
        TRANSPORT_TARGET_CLOSE(config->transport);
        ctx->transport_opened = 0;
    }

    return result;
}

ymodem_status_t ymodem_service_transmit(ymodem_config_t *config, ymodem_ctx_t *ctx,
                                        uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
    uint32_t errors = 0, ack_recpt = 0, size = 0, pkt_size;
    uint8_t *p_buf_int;
    ymodem_status_t result = YMODEM_OK;
    uint32_t blk_number = 1;
    uint8_t a_rx_ctrl[2];
    uint8_t i;
    uint8_t tmp;
#ifdef CRC16_F
    uint32_t temp_crc;
#else
    uint8_t temp_chksum;
#endif

    (void)ctx;

    PrepareIntialPacket(ctx->packet_data, p_file_name, file_size);

    while ((!ack_recpt) && (result == YMODEM_OK))
    {
        UART_TRANSMIT(config->uart, &ctx->packet_data[PACKET_START_INDEX],
                      PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

#ifdef CRC16_F
        temp_crc = ymodem_cal_crc16(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
        tmp = (temp_crc >> 8) & 0xFF;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);
        tmp = temp_crc & 0xFF;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);
#else
        temp_chksum = ymodem_calc_checksum(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
        UART_TRANSMIT(config->uart, &temp_chksum, 1, 100);
#endif

        uint8_t rx_byte;
        if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0)
        {
            a_rx_ctrl[0] = rx_byte;
            if (a_rx_ctrl[0] == ACK)
            {
                ack_recpt = 1;
            }
            else if (a_rx_ctrl[0] == CA)
            {
                if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0 && rx_byte == CA)
                {
                    UART_FLUSH(config->uart);
                    result = YMODEM_ABORT;
                }
            }
        }
        else
        {
            errors++;
        }

        if (errors >= MAX_ERRORS)
        {
            result = YMODEM_ERROR;
        }
    }

    p_buf_int = p_buf;
    size = file_size;

    while ((size) && (result == YMODEM_OK))
    {
        PreparePacket(p_buf_int, ctx->packet_data, blk_number, size);
        ack_recpt = 0;
        a_rx_ctrl[0] = 0;
        errors = 0;

        while ((!ack_recpt) && (result == YMODEM_OK))
        {
            if (size >= PACKET_1K_SIZE)
            {
                pkt_size = PACKET_1K_SIZE;
            }
            else
            {
                pkt_size = PACKET_SIZE;
            }

            UART_TRANSMIT(config->uart, &ctx->packet_data[PACKET_START_INDEX],
                          pkt_size + PACKET_HEADER_SIZE, NAK_TIMEOUT);

#ifdef CRC16_F
            temp_crc = ymodem_cal_crc16(&ctx->packet_data[PACKET_DATA_INDEX], pkt_size);
            tmp = (temp_crc >> 8) & 0xFF;
            UART_TRANSMIT(config->uart, &tmp, 1, 100);
            tmp = temp_crc & 0xFF;
            UART_TRANSMIT(config->uart, &tmp, 1, 100);
#else
            temp_chksum = ymodem_calc_checksum(&ctx->packet_data[PACKET_DATA_INDEX], pkt_size);
            UART_TRANSMIT(config->uart, &temp_chksum, 1, 100);
#endif

            uint8_t rx_byte;
            if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0 && rx_byte == ACK)
            {
                a_rx_ctrl[0] = rx_byte;
                ack_recpt = 1;
                if (size > pkt_size)
                {
                    p_buf_int += pkt_size;
                    size -= pkt_size;
                    if (blk_number == (config->max_size / PACKET_1K_SIZE))
                    {
                        result = YMODEM_LIMIT;
                    }
                    else
                    {
                        blk_number++;
                    }
                }
                else
                {
                    p_buf_int += pkt_size;
                    size = 0;
                }
            }
            else
            {
                errors++;
            }

            if (errors >= MAX_ERRORS)
            {
                result = YMODEM_ERROR;
            }
        }
    }

    ack_recpt = 0;
    a_rx_ctrl[0] = 0x00;
    errors = 0;

    while ((!ack_recpt) && (result == YMODEM_OK))
    {
        tmp = EOT;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);

        uint8_t rx_byte;
        if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0)
        {
            a_rx_ctrl[0] = rx_byte;
            if (a_rx_ctrl[0] == ACK)
            {
                ack_recpt = 1;
            }
            else if (a_rx_ctrl[0] == CA)
            {
                if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0 && rx_byte == CA)
                {
                    UART_FLUSH(config->uart);
                    result = YMODEM_ABORT;
                }
            }
        }
        else
        {
            errors++;
        }

        if (errors >= MAX_ERRORS)
        {
            result = YMODEM_ERROR;
        }
    }

    if (result == YMODEM_OK)
    {
        ctx->packet_data[PACKET_START_INDEX] = SOH;
        ctx->packet_data[PACKET_NUMBER_INDEX] = 0;
        ctx->packet_data[PACKET_CNUMBER_INDEX] = 0xFF;

        for (i = PACKET_DATA_INDEX; i < (PACKET_SIZE + PACKET_DATA_INDEX); i++)
        {
            ctx->packet_data[i] = 0x00;
        }

        UART_TRANSMIT(config->uart, &ctx->packet_data[PACKET_START_INDEX],
                      PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

#ifdef CRC16_F
        temp_crc = ymodem_cal_crc16(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
        tmp = (temp_crc >> 8) & 0xFF;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);
        tmp = temp_crc & 0xFF;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);
#else
        temp_chksum = ymodem_calc_checksum(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
        UART_TRANSMIT(config->uart, &temp_chksum, 1, 100);
#endif

        uint8_t rx_byte;
        if (UART_RECEIVE(config->uart, &rx_byte, 1, NAK_TIMEOUT) == 0)
        {
            if (rx_byte == CA)
            {
                UART_FLUSH(config->uart);
                result = YMODEM_ABORT;
            }
        }
    }

    return result;
}
