#include "service_ymodem.h"
#include <string.h>

#define CRC16_F

#define YMODEM_HANDSHAKE_TIMEOUT_SEC 10
#define YMODEM_WAIT_CHR_TIMEOUT_MS 3000
#define YMODEM_WAIT_PKG_TIMEOUT_MS 3000
#define YMODEM_CHD_INTV_MS 1000

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
    uint32_t file_name_end;
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
    file_name_end = i + PACKET_DATA_INDEX + 1;

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

    for (i = 0; astring[i] != '\0'; i++)
    {
        p_data[file_name_end + i] = astring[i];
    }

    for (i = file_name_end + i; i < PACKET_SIZE + PACKET_DATA_INDEX; i++)
    {
        p_data[i] = 0;
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

static int16_t ReceiveByte(ymodem_config_t *config, uint8_t *data, uint32_t timeout)
{
    return UART_RECEIVE(config->uart, data, 1, timeout);
}

static int16_t SendByte(ymodem_config_t *config, uint8_t data)
{
    return UART_TRANSMIT(config->uart, &data, 1, 100);
}

static int16_t ReceivePacket(ymodem_config_t *config, ymodem_ctx_t *ctx, uint8_t *p_data, uint32_t *p_length, uint32_t timeout)
{
    uint32_t crc;
    uint32_t packet_size = 0;
    int16_t status;
    uint8_t char1;

    (void)ctx;

    *p_length = 0;
    status = ReceiveByte(config, &char1, timeout);

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
            if (ReceiveByte(config, &char1, timeout) == 0 && (char1 == CA))
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

static ymodem_status_t Ymodem_DoHandshake(ymodem_config_t *config, ymodem_ctx_t *ctx, uint32_t handshake_timeout_sec)
{
    uint32_t packet_length;
    int16_t recv_status;
    uint32_t i;

    for (i = 0; i < handshake_timeout_sec; i++)
    {
        SendByte(config, CRC16);

        recv_status = ReceivePacket(config, ctx, ctx->packet_data, &packet_length, YMODEM_CHD_INTV_MS);

        if (recv_status == 0)
        {
            if (packet_length >= PACKET_SIZE)
            {
                if (ctx->packet_data[PACKET_NUMBER_INDEX] == 0x00 &&
                    ctx->packet_data[PACKET_CNUMBER_INDEX] == 0xFF)
                {
                    return YMODEM_OK;
                }
            }
        }
        else if (recv_status == -2)
        {
            return YMODEM_ABORT;
        }
    }

    return YMODEM_TIMEOUT;
}

static ymodem_status_t Ymodem_DoSendHandshake(ymodem_config_t *config, ymodem_ctx_t *ctx,
                                              const uint8_t *p_file_name, uint32_t file_size)
{
    uint8_t code;
    uint32_t i;
    uint8_t ack_char;
    uint16_t temp_crc;
    uint8_t tmp;

    for (i = 0; i < YMODEM_HANDSHAKE_TIMEOUT_SEC; i++)
    {
        if (ReceiveByte(config, &code, YMODEM_CHD_INTV_MS) == 0)
        {
            if (code == CRC16)
            {
                break;
            }
        }
    }

    if (i == YMODEM_HANDSHAKE_TIMEOUT_SEC)
    {
        return YMODEM_TIMEOUT;
    }

    UART_FLUSH(config->uart);

    PrepareIntialPacket(ctx->packet_data, p_file_name, file_size);

    UART_TRANSMIT(config->uart, &ctx->packet_data[PACKET_START_INDEX],
                  PACKET_SIZE + PACKET_HEADER_SIZE, NAK_TIMEOUT);

#ifdef CRC16_F
    temp_crc = ymodem_cal_crc16(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
    tmp = (temp_crc >> 8) & 0xFF;
    UART_TRANSMIT(config->uart, &tmp, 1, 100);
    tmp = temp_crc & 0xFF;
    UART_TRANSMIT(config->uart, &tmp, 1, 100);
#else
    uint8_t temp_chksum = ymodem_calc_checksum(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
    UART_TRANSMIT(config->uart, &temp_chksum, 1, 100);
#endif

    if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) != 0 || ack_char != ACK)
    {
        i = 0;
        while (i < MAX_ERRORS)
        {
            if (ack_char == ACK)
            {
                break;
            }
            if (ack_char == CA)
            {
                if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0 && ack_char == CA)
                {
                    UART_FLUSH(config->uart);
                    return YMODEM_ABORT;
                }
            }
            if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) != 0)
            {
                return YMODEM_ERROR;
            }
            i++;
        }
        if (i >= MAX_ERRORS)
        {
            return YMODEM_ERROR;
        }
    }

    {
        i = 0;
        while (i < MAX_ERRORS)
        {
            if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) != 0)
            {
                return YMODEM_ERROR;
            }
            if (ack_char == CRC16)
            {
                break;
            }
            if (ack_char == CA)
            {
                if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0 && ack_char == CA)
                {
                    UART_FLUSH(config->uart);
                    return YMODEM_ABORT;
                }
            }
            i++;
        }
        if (i >= MAX_ERRORS)
        {
            return YMODEM_ERROR;
        }
    }

    return YMODEM_OK;
}

static ymodem_status_t Ymodem_DoTrans(ymodem_config_t *config, ymodem_ctx_t *ctx)
{
    uint32_t packet_length;
    int16_t recv_status;
    ymodem_status_t result = YMODEM_OK;

    SendByte(config, ACK);
    SendByte(config, CRC16);

    ctx->packets_received = 1;
    ctx->errors = 0;

    while (result == YMODEM_OK)
    {
        recv_status = ReceivePacket(config, ctx, ctx->packet_data, &packet_length, YMODEM_WAIT_PKG_TIMEOUT_MS);

        if (recv_status == 0)
        {
            ctx->errors = 0;

            switch (packet_length)
            {
            case 2:
                SendByte(config, ACK);
                result = YMODEM_ABORT;
                break;

            case 0:
                return YMODEM_OK;

            default:
                if (ctx->packet_data[PACKET_NUMBER_INDEX] == 0x00 &&
                    ctx->packet_data[PACKET_CNUMBER_INDEX] == 0xFF)
                {
                    SendByte(config, ACK);
                    SendByte(config, CRC16);
                    continue;
                }

                if (ctx->packet_data[PACKET_NUMBER_INDEX] != (uint8_t)ctx->packets_received)
                {
                    SendByte(config, NAK);
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
                            SendByte(config, ACK);
                        }
                        else
                        {
                            SendByte(config, CA);
                            SendByte(config, CA);
                            result = YMODEM_DATA;
                        }
                    }
                    else
                    {
                        ctx->write_offset += packet_length;
                        SendByte(config, ACK);
                    }

                    ctx->packets_received++;
                }
                break;
            }
        }
        else if (recv_status == -2)
        {
            SendByte(config, CA);
            SendByte(config, CA);
            result = YMODEM_ABORT;
        }
        else
        {
            ctx->errors++;
            if (ctx->errors > MAX_ERRORS)
            {
                SendByte(config, CA);
                SendByte(config, CA);
                result = YMODEM_ERROR;
            }
            else
            {
                SendByte(config, NAK);
            }
        }
    }

    return result;
}

static ymodem_status_t Ymodem_DoFin(ymodem_config_t *config, ymodem_ctx_t *ctx)
{
    uint32_t packet_length;
    int16_t recv_status;
    uint16_t recv_crc, cal_crc;
    uint32_t data_sz;

    SendByte(config, NAK);

    recv_status = ReceivePacket(config, ctx, ctx->packet_data, &packet_length, YMODEM_WAIT_PKG_TIMEOUT_MS);
    if (recv_status != 0 || packet_length != 0)
    {
        return YMODEM_ERROR;
    }

    SendByte(config, ACK);
    SendByte(config, CRC16);

    recv_status = ReceivePacket(config, ctx, ctx->packet_data, &packet_length, YMODEM_WAIT_PKG_TIMEOUT_MS);
    if (recv_status != 0)
    {
        return YMODEM_ERROR;
    }

    if (packet_length == PACKET_SIZE)
    {
        data_sz = PACKET_SIZE;
    }
    else if (packet_length == PACKET_1K_SIZE)
    {
        data_sz = PACKET_1K_SIZE;
    }
    else
    {
        return YMODEM_ERROR;
    }

    if (ctx->packet_data[PACKET_NUMBER_INDEX] != 0 || ctx->packet_data[PACKET_CNUMBER_INDEX] != 0xFF)
    {
        return YMODEM_ERROR;
    }

    recv_crc = (uint16_t)(ctx->packet_data[data_sz + PACKET_DATA_INDEX] << 8) |
               ctx->packet_data[data_sz + PACKET_DATA_INDEX + 1];
    cal_crc = ymodem_cal_crc16(&ctx->packet_data[PACKET_DATA_INDEX], data_sz);

    if (recv_crc != cal_crc)
    {
        return YMODEM_ERROR;
    }

    if (ctx->packet_data[PACKET_DATA_INDEX] != 0)
    {
        SendByte(config, ACK);
        return YMODEM_OK;
    }

    SendByte(config, ACK);

    return YMODEM_OK;
}

ymodem_status_t ymodem_service_receive(ymodem_config_t *config, ymodem_ctx_t *ctx, uint32_t *p_size)
{
    uint32_t i, filesize;
    uint8_t *file_ptr;
    uint8_t file_size[FILE_SIZE_LENGTH], tmp;
    ymodem_status_t result = YMODEM_OK;

    ctx->write_offset = 0;
    ctx->session_done = 0;
    ctx->errors = 0;
    ctx->session_begin = 0;
    ctx->transport_opened = 0;
    ctx->total_size = 0;

    UART_FLUSH(config->uart);

    result = Ymodem_DoHandshake(config, ctx, YMODEM_HANDSHAKE_TIMEOUT_SEC);
    if (result != YMODEM_OK)
    {
        return result;
    }

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
            return YMODEM_LIMIT;
        }

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
                return YMODEM_DATA;
            }
        }

        result = Ymodem_DoTrans(config, ctx);
        if (result != YMODEM_OK)
        {
            goto __cleanup;
        }

        result = Ymodem_DoFin(config, ctx);
        if (result != YMODEM_OK)
        {
            goto __cleanup;
        }
    }
    else
    {
        SendByte(config, ACK);
    }

__cleanup:
    if (ctx->transport_opened && config->transport)
    {
        TRANSPORT_TARGET_CLOSE(config->transport);
        ctx->transport_opened = 0;
    }

    return result;
}

static ymodem_status_t Ymodem_DoSendTrans(ymodem_config_t *config, ymodem_ctx_t *ctx,
                                          uint8_t *p_buf, uint32_t file_size)
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
            if (ReceiveByte(config, &rx_byte, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0)
            {
                a_rx_ctrl[0] = rx_byte;
                if (a_rx_ctrl[0] == ACK)
                {
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
                else if (a_rx_ctrl[0] == CA)
                {
                    if (ReceiveByte(config, &rx_byte, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0 && rx_byte == CA)
                    {
                        UART_FLUSH(config->uart);
                        result = YMODEM_ABORT;
                    }
                }
                else if (a_rx_ctrl[0] == NAK)
                {
                    errors++;
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

    return result;
}

static ymodem_status_t Ymodem_DoSendFin(ymodem_config_t *config, ymodem_ctx_t *ctx)
{
    uint8_t ack_char;
    uint8_t tmp;
    uint16_t temp_crc;
    uint32_t errors = 0;
    ymodem_status_t result = YMODEM_OK;
    uint8_t i;

    errors = 0;
    while (errors < MAX_ERRORS && result == YMODEM_OK)
    {
        tmp = EOT;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);

        if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0)
        {
            if (ack_char == NAK)
            {
                break;
            }
            else if (ack_char == ACK)
            {
                break;
            }
            else if (ack_char == CA)
            {
                if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0 && ack_char == CA)
                {
                    UART_FLUSH(config->uart);
                    return YMODEM_ABORT;
                }
            }
        }
        errors++;
    }

    if (errors >= MAX_ERRORS)
    {
        return YMODEM_ERROR;
    }

    errors = 0;
    while (errors < MAX_ERRORS && result == YMODEM_OK)
    {
        tmp = EOT;
        UART_TRANSMIT(config->uart, &tmp, 1, 100);

        if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0)
        {
            if (ack_char == ACK)
            {
                break;
            }
            else if (ack_char == CA)
            {
                if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0 && ack_char == CA)
                {
                    UART_FLUSH(config->uart);
                    return YMODEM_ABORT;
                }
            }
        }
        errors++;
    }

    if (errors >= MAX_ERRORS)
    {
        return YMODEM_ERROR;
    }

    if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) != 0 || ack_char != CRC16)
    {
        return YMODEM_ERROR;
    }

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
    uint8_t temp_chksum = ymodem_calc_checksum(&ctx->packet_data[PACKET_DATA_INDEX], PACKET_SIZE);
    UART_TRANSMIT(config->uart, &temp_chksum, 1, 100);
#endif

    if (ReceiveByte(config, &ack_char, YMODEM_WAIT_CHR_TIMEOUT_MS) == 0)
    {
        if (ack_char == ACK)
        {
            result = YMODEM_OK;
        }
        else if (ack_char == CA)
        {
            UART_FLUSH(config->uart);
            result = YMODEM_ABORT;
        }
        else
        {
            result = YMODEM_ERROR;
        }
    }
    else
    {
        result = YMODEM_ERROR;
    }

    return result;
}

ymodem_status_t ymodem_service_transmit(ymodem_config_t *config, ymodem_ctx_t *ctx,
                                        uint8_t *p_buf, const uint8_t *p_file_name, uint32_t file_size)
{
    ymodem_status_t result = YMODEM_OK;

    result = Ymodem_DoSendHandshake(config, ctx, p_file_name, file_size);
    if (result != YMODEM_OK)
    {
        return result;
    }

    result = Ymodem_DoSendTrans(config, ctx, p_buf, file_size);
    if (result != YMODEM_OK)
    {
        return result;
    }

    result = Ymodem_DoSendFin(config, ctx);

    return result;
}
