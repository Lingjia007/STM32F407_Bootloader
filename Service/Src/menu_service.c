#include "menu_service.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

static char g_printf_buffer[MENU_MAX_PROMPT_LEN];

void menu_service_init(menu_ctx_t *ctx, platform_uart_base_t *uart)
{
    if (ctx == NULL || uart == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(menu_ctx_t));
    ctx->uart = uart;
    ctx->echo_enabled = true;
    ctx->show_help_on_enter = true;
    ctx->show_description = true;
    ctx->single_key_mode = true;
    ctx->running = false;
    strcpy(ctx->prompt, "Menu");
}

void menu_service_set_root(menu_ctx_t *ctx, const menu_item_t *menu, uint8_t count)
{
    if (ctx == NULL || menu == NULL)
    {
        return;
    }

    ctx->root_menu = menu;
    ctx->root_count = count;
    ctx->nav.depth = 0;
    ctx->nav.stack[0] = menu;
    ctx->nav.selected[0] = 0;
}

void menu_service_set_prompt(menu_ctx_t *ctx, const char *prompt)
{
    if (ctx == NULL || prompt == NULL)
    {
        return;
    }

    strncpy(ctx->prompt, prompt, MENU_MAX_PROMPT_LEN - 1);
    ctx->prompt[MENU_MAX_PROMPT_LEN - 1] = '\0';
}

void menu_service_print(menu_ctx_t *ctx, const char *str)
{
    if (ctx == NULL || str == NULL)
    {
        return;
    }

    uint16_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }

    UART_TRANSMIT(ctx->uart, (const uint8_t *)str, len, 1000);
}

void menu_service_println(menu_ctx_t *ctx, const char *str)
{
    menu_service_print(ctx, str);
    menu_service_print(ctx, "\r\n");
}

void menu_service_printn(menu_ctx_t *ctx, const char *str, int n)
{
    if (ctx == NULL || str == NULL || n <= 0)
    {
        return;
    }

    int len = strlen(str);
    if (n < len)
        len = n;

    UART_TRANSMIT(ctx->uart, (const uint8_t *)str, (uint16_t)len, 1000);
}

void menu_service_printf(menu_ctx_t *ctx, const char *fmt, ...)
{
    if (ctx == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    int len = vsnprintf(g_printf_buffer, sizeof(g_printf_buffer), fmt, args);
    va_end(args);

    if (len > 0)
    {
        UART_TRANSMIT(ctx->uart, (uint8_t *)g_printf_buffer, (uint16_t)len, 1000);
    }
}

void menu_service_print_banner(menu_ctx_t *ctx, const char *title)
{
    if (ctx == NULL || title == NULL)
    {
        return;
    }

    menu_service_print(ctx, "\r\n");
    menu_service_print_separator(ctx, "=", 60);
    menu_service_print(ctx, "\r\n");

    size_t len = strlen(title);
    size_t padding = (60 - len) / 2;

    for (size_t i = 0; i < padding; i++)
    {
        menu_service_print(ctx, " ");
    }
    menu_service_println(ctx, title);

    menu_service_print_separator(ctx, "=", 60);
    menu_service_print(ctx, "\r\n\r\n");
}

void menu_service_print_separator(menu_ctx_t *ctx, const char *ch, uint8_t len)
{
    if (ctx == NULL || ch == NULL || len == 0)
    {
        return;
    }

    for (uint8_t i = 0; i < len; i++)
    {
        menu_service_print(ctx, ch);
    }
}

void menu_service_print_progress(menu_ctx_t *ctx, const char *label, int pct)
{
    if (ctx == NULL)
        return;
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;

    char buf[96];
    int pos = 0;

    if (label != NULL)
    {
        while (*label && pos < 30)
            buf[pos++] = *label++;
        buf[pos++] = ' ';
    }

    buf[pos++] = '[';
    int bar_width = 40;
    int filled = (pct * bar_width + 50) / 100;
    for (int i = 0; i < bar_width; i++)
    {
        if (i < filled)
            buf[pos++] = '=';
        else
            buf[pos++] = ' ';
    }
    buf[pos++] = ']';

    pos += snprintf(&buf[pos], sizeof(buf) - pos, " %3d%%", pct);

    menu_service_print(ctx, "\r");
    menu_service_print(ctx, buf);
    menu_service_print(ctx, "\r\n");
}

int16_t menu_service_getchar(menu_ctx_t *ctx, uint8_t *ch, uint32_t timeout)
{
    if (ctx == NULL || ch == NULL)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    return UART_RECEIVE(ctx->uart, ch, 1, timeout);
}

int16_t menu_service_getline(menu_ctx_t *ctx, char *buf, uint16_t max_len, uint32_t timeout)
{
    if (ctx == NULL || buf == NULL || max_len == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    ctx->input_index = 0;
    uint8_t ch;

    while (ctx->input_index < max_len - 1)
    {
        if (UART_RECEIVE(ctx->uart, &ch, 1, timeout) != (int16_t)UART_STATUS_OK)
        {
            return (int16_t)UART_STATUS_TIMEOUT;
        }

        if (ch == '\r' || ch == '\n')
        {
            buf[ctx->input_index] = '\0';
            menu_service_print(ctx, "\r\n");
            return (int16_t)UART_STATUS_OK;
        }
        else if (ch == 0x08 || ch == 0x7F)
        {
            if (ctx->input_index > 0)
            {
                ctx->input_index--;
                menu_service_print(ctx, "\b \b");
            }
        }
        else if (ch == 0x1B)
        {
            if (UART_RECEIVE(ctx->uart, &ch, 1, 10) == (int16_t)UART_STATUS_OK)
            {
                if (ch == '[')
                {
                    UART_RECEIVE(ctx->uart, &ch, 1, 10);
                }
            }
        }
        else if (ch >= 0x20 && ch <= 0x7E)
        {
            buf[ctx->input_index++] = ch;
            if (ctx->echo_enabled)
            {
                UART_TRANSMIT(ctx->uart, &ch, 1, 100);
            }
        }
    }

    buf[ctx->input_index] = '\0';
    return (int16_t)UART_STATUS_OK;
}

int16_t menu_service_getline_with_history(menu_ctx_t *ctx, char *buf, uint16_t max_len, uint32_t timeout)
{
    if (ctx == NULL || buf == NULL || max_len == 0)
    {
        return (int16_t)UART_STATUS_PARAM;
    }

    ctx->input_index = 0;
    uint8_t ch;
    int8_t history_index = ctx->history.count;

    while (ctx->input_index < max_len - 1)
    {
        if (UART_RECEIVE(ctx->uart, &ch, 1, timeout) != (int16_t)UART_STATUS_OK)
        {
            return (int16_t)UART_STATUS_TIMEOUT;
        }

        if (ch == '\r' || ch == '\n')
        {
            buf[ctx->input_index] = '\0';
            menu_service_print(ctx, "\r\n");
            if (ctx->input_index > 0)
            {
                menu_service_add_history(ctx, buf);
            }
            return (int16_t)UART_STATUS_OK;
        }
        else if (ch == 0x08 || ch == 0x7F)
        {
            if (ctx->input_index > 0)
            {
                ctx->input_index--;
                menu_service_print(ctx, "\b \b");
            }
        }
        else if (ch == 0x1B)
        {
            uint8_t ch2, ch3;
            if (UART_RECEIVE(ctx->uart, &ch2, 1, 10) == (int16_t)UART_STATUS_OK)
            {
                if (ch2 == '[')
                {
                    if (UART_RECEIVE(ctx->uart, &ch3, 1, 10) == (int16_t)UART_STATUS_OK)
                    {
                        if (ch3 == 'A')
                        {
                            if (history_index > 0)
                            {
                                history_index--;
                                while (ctx->input_index > 0)
                                {
                                    ctx->input_index--;
                                    menu_service_print(ctx, "\b \b");
                                }
                                strcpy(buf, ctx->history.history[history_index]);
                                ctx->input_index = strlen(buf);
                                menu_service_print(ctx, buf);
                            }
                        }
                        else if (ch3 == 'B')
                        {
                            if (history_index < ctx->history.count - 1)
                            {
                                history_index++;
                                while (ctx->input_index > 0)
                                {
                                    ctx->input_index--;
                                    menu_service_print(ctx, "\b \b");
                                }
                                strcpy(buf, ctx->history.history[history_index]);
                                ctx->input_index = strlen(buf);
                                menu_service_print(ctx, buf);
                            }
                            else if (history_index == ctx->history.count - 1)
                            {
                                history_index = ctx->history.count;
                                while (ctx->input_index > 0)
                                {
                                    ctx->input_index--;
                                    menu_service_print(ctx, "\b \b");
                                }
                                buf[0] = '\0';
                                ctx->input_index = 0;
                            }
                        }
                    }
                }
            }
        }
        else if (ch >= 0x20 && ch <= 0x7E)
        {
            buf[ctx->input_index++] = ch;
            if (ctx->echo_enabled)
            {
                UART_TRANSMIT(ctx->uart, &ch, 1, 100);
            }
        }
    }

    buf[ctx->input_index] = '\0';
    return (int16_t)UART_STATUS_OK;
}

void menu_service_flush(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    UART_FLUSH(ctx->uart);
}

void menu_service_clear_input(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    ctx->input_index = 0;
    memset(ctx->input_buffer, 0, sizeof(ctx->input_buffer));
}

void menu_service_clear_screen(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    menu_service_print(ctx, "\033[2J\033[H");
}

void menu_service_int2str(uint8_t *p_str, uint32_t intnum)
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

uint32_t menu_service_str2int(uint8_t *p_inputstr, uint32_t *p_intnum)
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
                res = 0;
                break;
            }
            i++;
        }

        if (p_inputstr[i] == '\0')
        {
            *p_intnum = val;
            res = 1;
        }
    }
    else
    {
        while ((i < 11) && (res != 1))
        {
            if (p_inputstr[i] == '\0')
            {
                *p_intnum = val;
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
                res = 0;
                break;
            }
            i++;
        }
    }

    return res;
}

int menu_service_parse_args(menu_ctx_t *ctx, char *input)
{
    if (ctx == NULL || input == NULL)
    {
        return 0;
    }

    ctx->argc = 0;
    memset(ctx->argv, 0, sizeof(ctx->argv));

    char *token = strtok(input, " \t");
    while (token != NULL && ctx->argc < MENU_MAX_ARGS)
    {
        strncpy(ctx->argv[ctx->argc], token, MENU_MAX_ARG_LEN - 1);
        ctx->argv[ctx->argc][MENU_MAX_ARG_LEN - 1] = '\0';
        ctx->argc++;
        token = strtok(NULL, " \t");
    }

    return ctx->argc;
}

const menu_item_t *menu_service_get_current_menu(menu_ctx_t *ctx, uint8_t *count)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    if (ctx->nav.depth == 0)
    {
        if (count != NULL)
        {
            *count = ctx->root_count;
        }
        return ctx->root_menu;
    }
    else
    {
        const menu_item_t *parent = ctx->nav.stack[ctx->nav.depth - 1];
        if (parent != NULL && parent->type == MENU_ITEM_TYPE_SUBMENU)
        {
            if (count != NULL)
            {
                *count = parent->submenu_count;
            }
            return parent->submenu;
        }
    }

    if (count != NULL)
    {
        *count = 0;
    }
    return NULL;
}

void menu_service_show_menu(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    uint8_t count = 0;
    const menu_item_t *menu = menu_service_get_current_menu(ctx, &count);

    if (menu == NULL || count == 0)
    {
        menu_service_println(ctx, "No menu items available.");
        return;
    }

    int total_width = ctx->show_description ? 80 : 50;
    int name_width = ctx->show_description ? 42 : 30;

    menu_service_print(ctx, "\r\n");
    menu_service_print_separator(ctx, "=", total_width);
    menu_service_print(ctx, "\r\n");

    if (ctx->nav.depth == 0)
    {
        menu_service_printf(ctx, "  %s Menu\r\n", ctx->prompt);
    }
    else
    {
        const menu_item_t *parent = ctx->nav.stack[ctx->nav.depth - 1];
        if (parent != NULL)
        {
            menu_service_printf(ctx, "  %s\r\n", parent->name);
        }
    }

    menu_service_print_separator(ctx, "=", total_width);
    menu_service_print(ctx, "\r\n\r\n");

    for (uint8_t i = 0; i < count; i++)
    {
        const menu_item_t *item = &menu[i];

        if (item->flags & MENU_FLAG_HIDDEN)
        {
            continue;
        }

        if (item->type == MENU_ITEM_TYPE_SEPARATOR)
        {
            menu_service_print_separator(ctx, "-", total_width);
            menu_service_print(ctx, "\r\n");
            continue;
        }

        if (item->key != NULL)
        {
            menu_service_printf(ctx, "  [%s] ", item->key);

            int name_len = strlen(item->name);
            if (name_len > name_width)
                name_len = name_width;

            menu_service_printn(ctx, item->name, name_len);

            if (ctx->show_description)
            {
                for (int j = name_len; j < name_width; j++)
                {
                    menu_service_print(ctx, " ");
                }

                if (item->description != NULL)
                {
                    menu_service_print(ctx, "- ");
                    menu_service_print(ctx, item->description);
                }

                if (item->type == MENU_ITEM_TYPE_SUBMENU)
                {
                    menu_service_print(ctx, " >>");
                }
            }
            else
            {
                if (item->type == MENU_ITEM_TYPE_SUBMENU)
                {
                    menu_service_print(ctx, " >>");
                }
            }

            if (item->flags & MENU_FLAG_DISABLED)
            {
                menu_service_print(ctx, " (disabled)");
            }

            menu_service_print(ctx, "\r\n");
        }
    }

    menu_service_print(ctx, "\r\n");
    menu_service_print_separator(ctx, "=", total_width);
    menu_service_print(ctx, "\r\n\r\n");

    if (ctx->single_key_mode)
    {
        menu_service_print(ctx, "Enter selection: ");
    }
    else
    {
        menu_service_print(ctx, "Enter selection (or 'h' for help, 'q' to quit): ");
    }
}

void menu_service_show_help(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    menu_service_println(ctx, "\r\n=== Help ===");
    menu_service_println(ctx, "Available commands:");
    menu_service_println(ctx, "  <key>  - Select menu item (case insensitive)");
    menu_service_println(ctx, "  H      - Show this help");
    menu_service_println(ctx, "  ?      - Show this help");
    menu_service_println(ctx, "  Q      - Exit menu system");
    menu_service_println(ctx, "  clear  - Clear screen (in line mode)");
    menu_service_println(ctx, "");
    menu_service_println(ctx, "Settings:");
    menu_service_printf(ctx, "  D      - Toggle description (current: %s)\r\n", ctx->show_description ? "ON" : "OFF");
    menu_service_printf(ctx, "  S      - Toggle single key mode (current: %s)\r\n", ctx->single_key_mode ? "ON" : "OFF");
    menu_service_println(ctx, "");
}

void menu_service_show_item_help(menu_ctx_t *ctx, const menu_item_t *item)
{
    if (ctx == NULL || item == NULL)
    {
        return;
    }

    menu_service_println(ctx, "\r\n=== Item Help ===");
    menu_service_printf(ctx, "Key:         %s\r\n", item->key ? item->key : "N/A");
    menu_service_printf(ctx, "Name:        %s\r\n", item->name ? item->name : "N/A");
    menu_service_printf(ctx, "Description: %s\r\n", item->description ? item->description : "N/A");

    const char *type_str = "Unknown";
    switch (item->type)
    {
    case MENU_ITEM_TYPE_COMMAND:
        type_str = "Command";
        break;
    case MENU_ITEM_TYPE_SUBMENU:
        type_str = "Submenu";
        break;
    case MENU_ITEM_TYPE_SEPARATOR:
        type_str = "Separator";
        break;
    case MENU_ITEM_TYPE_BACK:
        type_str = "Back";
        break;
    }
    menu_service_printf(ctx, "Type:        %s\r\n", type_str);

    if (item->flags & MENU_FLAG_DISABLED)
    {
        menu_service_println(ctx, "Status:      Disabled");
    }
    if (item->flags & MENU_FLAG_CONFIRM)
    {
        menu_service_println(ctx, "Requires confirmation before execution");
    }

    menu_service_println(ctx, "");
}

const menu_item_t *menu_service_find_item(menu_ctx_t *ctx, const char *key)
{
    if (ctx == NULL || key == NULL)
    {
        return NULL;
    }

    uint8_t count = 0;
    const menu_item_t *menu = menu_service_get_current_menu(ctx, &count);

    if (menu == NULL)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < count; i++)
    {
        const menu_item_t *item = &menu[i];

        if (item->key != NULL && strcmp(item->key, key) == 0)
        {
            return item;
        }
    }

    return NULL;
}

bool menu_service_navigate(menu_ctx_t *ctx, const char *key)
{
    if (ctx == NULL || key == NULL)
    {
        return false;
    }

    const menu_item_t *item = menu_service_find_item(ctx, key);

    if (item == NULL)
    {
        return false;
    }

    if (item->flags & MENU_FLAG_DISABLED)
    {
        menu_service_println(ctx, "This item is currently disabled.");
        return false;
    }

    switch (item->type)
    {
    case MENU_ITEM_TYPE_COMMAND:
        if (item->handler != NULL)
        {
            if (item->flags & MENU_FLAG_CONFIRM)
            {
                menu_service_printf(ctx, "Confirm execution of '%s'? (y/n): ", item->name);
                uint8_t confirm;
                menu_service_flush(ctx);
                if (menu_service_getchar(ctx, &confirm, 0xFFFFFFFF) == (int16_t)UART_STATUS_OK)
                {
                    menu_service_print(ctx, "\r\n");
                    if (confirm == 'y' || confirm == 'Y')
                    {
                        item->handler(ctx, ctx->argc, (char **)ctx->argv);
                    }
                    else
                    {
                        menu_service_println(ctx, "Cancelled.");
                    }
                }
            }
            else
            {
                item->handler(ctx, ctx->argc, (char **)ctx->argv);
            }
        }
        break;

    case MENU_ITEM_TYPE_SUBMENU:
        if (ctx->nav.depth < MENU_MAX_DEPTH - 1)
        {
            if (item->on_enter != NULL)
            {
                item->on_enter(ctx);
            }
            ctx->nav.stack[ctx->nav.depth] = item;
            ctx->nav.depth++;
            ctx->nav.selected[ctx->nav.depth] = 0;
        }
        else
        {
            menu_service_println(ctx, "Maximum menu depth reached.");
        }
        break;

    case MENU_ITEM_TYPE_BACK:
        menu_service_navigate_back(ctx);
        break;

    default:
        break;
    }

    return true;
}

bool menu_service_navigate_back(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return false;
    }

    if (ctx->nav.depth > 0)
    {
        const menu_item_t *current = ctx->nav.stack[ctx->nav.depth - 1];
        if (current != NULL && current->on_exit != NULL)
        {
            current->on_exit(ctx);
        }

        ctx->nav.depth--;
        return true;
    }

    return false;
}

void menu_service_navigate_home(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    while (ctx->nav.depth > 0)
    {
        menu_service_navigate_back(ctx);
    }
}

void menu_service_add_history(menu_ctx_t *ctx, const char *cmd)
{
    if (ctx == NULL || cmd == NULL || strlen(cmd) == 0)
    {
        return;
    }

    if (ctx->history.count > 0 &&
        strcmp(ctx->history.history[ctx->history.count - 1], cmd) == 0)
    {
        return;
    }

    if (ctx->history.count < MENU_MAX_HISTORY)
    {
        strncpy(ctx->history.history[ctx->history.count], cmd, MENU_MAX_INPUT_LEN - 1);
        ctx->history.history[ctx->history.count][MENU_MAX_INPUT_LEN - 1] = '\0';
        ctx->history.count++;
    }
    else
    {
        for (uint8_t i = 0; i < MENU_MAX_HISTORY - 1; i++)
        {
            strcpy(ctx->history.history[i], ctx->history.history[i + 1]);
        }
        strncpy(ctx->history.history[MENU_MAX_HISTORY - 1], cmd, MENU_MAX_INPUT_LEN - 1);
        ctx->history.history[MENU_MAX_HISTORY - 1][MENU_MAX_INPUT_LEN - 1] = '\0';
    }

    ctx->history.current = ctx->history.count;
}

void menu_service_clear_history(menu_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx->history.history, 0, sizeof(ctx->history.history));
    ctx->history.count = 0;
    ctx->history.current = 0;
}

void menu_service_set_echo(menu_ctx_t *ctx, bool enabled)
{
    if (ctx != NULL)
    {
        ctx->echo_enabled = enabled;
    }
}

void menu_service_set_show_help(menu_ctx_t *ctx, bool enabled)
{
    if (ctx != NULL)
    {
        ctx->show_help_on_enter = enabled;
    }
}

void menu_service_set_show_description(menu_ctx_t *ctx, bool enabled)
{
    if (ctx != NULL)
    {
        ctx->show_description = enabled;
    }
}

void menu_service_set_single_key_mode(menu_ctx_t *ctx, bool enabled)
{
    if (ctx != NULL)
    {
        ctx->single_key_mode = enabled;
    }
}

void menu_service_stop(menu_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        ctx->running = false;
    }
}

void menu_service_run(menu_ctx_t *ctx)
{
    if (ctx == NULL || ctx->root_menu == NULL)
    {
        return;
    }

    ctx->running = true;

    while (ctx->running)
    {
        if (ctx->show_help_on_enter)
        {
            menu_service_show_menu(ctx);
        }

        menu_service_flush(ctx);

        if (ctx->single_key_mode)
        {
            uint8_t key = 0;
            int16_t result = menu_service_getchar(ctx, &key, 0xFFFFFFFF);

            if (result != (int16_t)UART_STATUS_OK)
            {
                continue;
            }

            if (ctx->echo_enabled)
            {
                char echo_buf[2] = {(char)key, '\0'};
                menu_service_println(ctx, echo_buf);
            }

            if (key == 'q' || key == 'Q')
            {
                menu_service_println(ctx, "Exiting menu...");
                ctx->running = false;
                continue;
            }

            if (key == 'h' || key == 'H' || key == '?')
            {
                menu_service_show_help(ctx);
                continue;
            }

            if (key == 'd' || key == 'D')
            {
                ctx->show_description = !ctx->show_description;
                menu_service_printf(ctx, "Description: %s\r\n", ctx->show_description ? "ON" : "OFF");
                continue;
            }

            if (key == 's' || key == 'S')
            {
                ctx->single_key_mode = !ctx->single_key_mode;
                menu_service_printf(ctx, "Single key mode: %s\r\n", ctx->single_key_mode ? "ON" : "OFF");
                continue;
            }

            char upper_key = (char)key;
            if (upper_key >= 'a' && upper_key <= 'z')
            {
                upper_key = upper_key - 'a' + 'A';
            }
            char cmd[2] = {upper_key, '\0'};

            if (!menu_service_navigate(ctx, cmd))
            {
                menu_service_printf(ctx, "Unknown key: '%c'. Type 'h' for help.\r\n", key);
            }
        }
        else
        {
            char input[MENU_MAX_INPUT_LEN];
            int16_t result = menu_service_getline_with_history(ctx, input, sizeof(input), 0xFFFFFFFF);

            if (result != (int16_t)UART_STATUS_OK)
            {
                continue;
            }

            menu_service_parse_args(ctx, input);

            if (ctx->argc == 0)
            {
                continue;
            }

            char *cmd = ctx->argv[0];

            if (strcmp(cmd, "q") == 0 || strcmp(cmd, "Q") == 0 ||
                strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0)
            {
                menu_service_println(ctx, "Exiting menu...");
                ctx->running = false;
                continue;
            }

            if (strcmp(cmd, "h") == 0 || strcmp(cmd, "H") == 0 ||
                strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0)
            {
                menu_service_show_help(ctx);
                continue;
            }

            if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0)
            {
                menu_service_clear_screen(ctx);
                continue;
            }

            if (strcmp(cmd, "d") == 0 || strcmp(cmd, "D") == 0 || strcmp(cmd, "desc") == 0)
            {
                ctx->show_description = !ctx->show_description;
                menu_service_printf(ctx, "Description: %s\r\n", ctx->show_description ? "ON" : "OFF");
                continue;
            }

            if (strcmp(cmd, "s") == 0 || strcmp(cmd, "S") == 0 || strcmp(cmd, "single") == 0)
            {
                ctx->single_key_mode = !ctx->single_key_mode;
                menu_service_printf(ctx, "Single key mode: %s\r\n", ctx->single_key_mode ? "ON" : "OFF");
                continue;
            }

            if (strcmp(cmd, "home") == 0)
            {
                menu_service_navigate_home(ctx);
                continue;
            }

            if (strcmp(cmd, "back") == 0)
            {
                if (!menu_service_navigate_back(ctx))
                {
                    menu_service_println(ctx, "Already at top level menu.");
                }
                continue;
            }

            char upper_cmd[2] = {cmd[0], '\0'};
            if (cmd[0] >= 'a' && cmd[0] <= 'z' && cmd[1] == '\0')
            {
                upper_cmd[0] = cmd[0] - 'a' + 'A';
            }

            if (!menu_service_navigate(ctx, upper_cmd))
            {
                menu_service_printf(ctx, "Unknown command: '%s'. Type 'h' for help.\r\n", cmd);
            }
        }
    }
}
