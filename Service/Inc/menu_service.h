#ifndef MENU_SERVICE_H
#define MENU_SERVICE_H

#include "platform_uart.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MENU_MAX_INPUT_LEN 256
#define MENU_MAX_PROMPT_LEN 256
#define MENU_MAX_ITEMS 32
#define MENU_MAX_DEPTH 4
#define MENU_MAX_NAME_LEN 32
#define MENU_MAX_DESC_LEN 64
#define MENU_MAX_HISTORY 16
#define MENU_MAX_ARGS 8
#define MENU_MAX_ARG_LEN 64

#define IS_CAP_LETTER(c) (((c) >= 'A') && ((c) <= 'F'))
#define IS_LC_LETTER(c) (((c) >= 'a') && ((c) <= 'f'))
#define IS_09(c) (((c) >= '0') && ((c) <= '9'))
#define ISVALIDHEX(c) (IS_CAP_LETTER(c) || IS_LC_LETTER(c) || IS_09(c))
#define ISVALIDDEC(c) IS_09(c)
#define CONVERTDEC(c) (c - '0')
#define CONVERTHEX_ALPHA(c) (IS_CAP_LETTER(c) ? ((c) - 'A' + 10) : ((c) - 'a' + 10))
#define CONVERTHEX(c) (IS_09(c) ? ((c) - '0') : CONVERTHEX_ALPHA(c))

typedef struct menu_item_s menu_item_t;
typedef struct menu_ctx_s menu_ctx_t;

typedef void (*menu_handler_t)(menu_ctx_t *ctx, int argc, char *argv[]);
typedef void (*menu_enter_callback_t)(menu_ctx_t *ctx);
typedef void (*menu_exit_callback_t)(menu_ctx_t *ctx);

typedef enum
{
    MENU_ITEM_TYPE_COMMAND = 0,
    MENU_ITEM_TYPE_SUBMENU = 1,
    MENU_ITEM_TYPE_SEPARATOR = 2,
    MENU_ITEM_TYPE_BACK = 3
} menu_item_type_t;

typedef enum
{
    MENU_FLAG_NONE = 0,
    MENU_FLAG_HIDDEN = (1 << 0),
    MENU_FLAG_DISABLED = (1 << 1),
    MENU_FLAG_CONFIRM = (1 << 2),
    MENU_FLAG_ADMIN = (1 << 3)
} menu_flag_t;

struct menu_item_s
{
    const char *key;
    const char *name;
    const char *description;
    menu_item_type_t type;
    uint8_t flags;
    menu_handler_t handler;
    const menu_item_t *submenu;
    uint8_t submenu_count;
    menu_enter_callback_t on_enter;
    menu_exit_callback_t on_exit;
};

typedef struct
{
    char arg[MENU_MAX_ARG_LEN];
} menu_arg_t;

typedef struct
{
    const menu_item_t *stack[MENU_MAX_DEPTH];
    uint8_t depth;
    uint8_t selected[MENU_MAX_DEPTH];
} menu_nav_t;

typedef struct
{
    char history[MENU_MAX_HISTORY][MENU_MAX_INPUT_LEN];
    uint8_t count;
    uint8_t current;
} menu_history_t;

struct menu_ctx_s
{
    platform_uart_base_t *uart;
    char input_buffer[MENU_MAX_INPUT_LEN];
    uint16_t input_index;

    const menu_item_t *root_menu;
    uint8_t root_count;
    menu_nav_t nav;
    menu_history_t history;

    menu_arg_t args[MENU_MAX_ARGS];
    int argc;
    char argv[MENU_MAX_ARGS][MENU_MAX_ARG_LEN];

    char prompt[MENU_MAX_PROMPT_LEN];
    bool running;
    bool echo_enabled;
    bool show_help_on_enter;
    bool show_description;
    bool single_key_mode;
};

void menu_service_init(menu_ctx_t *ctx, platform_uart_base_t *uart);
void menu_service_set_root(menu_ctx_t *ctx, const menu_item_t *menu, uint8_t count);
void menu_service_run(menu_ctx_t *ctx);
void menu_service_stop(menu_ctx_t *ctx);
void menu_service_set_prompt(menu_ctx_t *ctx, const char *prompt);

void menu_service_print(menu_ctx_t *ctx, const char *str);
void menu_service_println(menu_ctx_t *ctx, const char *str);
void menu_service_printn(menu_ctx_t *ctx, const char *str, int n);
void menu_service_printf(menu_ctx_t *ctx, const char *fmt, ...);
void menu_service_print_banner(menu_ctx_t *ctx, const char *title);
void menu_service_print_separator(menu_ctx_t *ctx, const char *ch, uint8_t len);
void menu_service_print_progress(menu_ctx_t *ctx, const char *label, int pct);

int16_t menu_service_getchar(menu_ctx_t *ctx, uint8_t *ch, uint32_t timeout);
int16_t menu_service_getline(menu_ctx_t *ctx, char *buf, uint16_t max_len, uint32_t timeout);
int16_t menu_service_getline_with_history(menu_ctx_t *ctx, char *buf, uint16_t max_len, uint32_t timeout);

void menu_service_flush(menu_ctx_t *ctx);
void menu_service_clear_input(menu_ctx_t *ctx);
void menu_service_clear_screen(menu_ctx_t *ctx);

void menu_service_int2str(uint8_t *p_str, uint32_t intnum);
uint32_t menu_service_str2int(uint8_t *inputstr, uint32_t *intnum);
int menu_service_parse_args(menu_ctx_t *ctx, char *input);

void menu_service_show_menu(menu_ctx_t *ctx);
void menu_service_show_help(menu_ctx_t *ctx);
void menu_service_show_item_help(menu_ctx_t *ctx, const menu_item_t *item);

bool menu_service_navigate(menu_ctx_t *ctx, const char *key);
bool menu_service_navigate_back(menu_ctx_t *ctx);
void menu_service_navigate_home(menu_ctx_t *ctx);

const menu_item_t *menu_service_find_item(menu_ctx_t *ctx, const char *key);
const menu_item_t *menu_service_get_current_menu(menu_ctx_t *ctx, uint8_t *count);

void menu_service_add_history(menu_ctx_t *ctx, const char *cmd);
void menu_service_clear_history(menu_ctx_t *ctx);

void menu_service_set_echo(menu_ctx_t *ctx, bool enabled);
void menu_service_set_show_help(menu_ctx_t *ctx, bool enabled);
void menu_service_set_show_description(menu_ctx_t *ctx, bool enabled);
void menu_service_set_single_key_mode(menu_ctx_t *ctx, bool enabled);

#define MENU_ITEM_CMD(key, name, desc, handler) \
    {key, name, desc, MENU_ITEM_TYPE_COMMAND, 0, handler, NULL, 0, NULL, NULL}

#define MENU_ITEM_SUBMENU(key, name, desc, submenu, count) \
    {key, name, desc, MENU_ITEM_TYPE_SUBMENU, 0, NULL, submenu, count, NULL, NULL}

#define MENU_ITEM_SUBMENU_CB(key, name, desc, submenu, count, on_enter, on_exit) \
    {key, name, desc, MENU_ITEM_TYPE_SUBMENU, 0, NULL, submenu, count, on_enter, on_exit}

#define MENU_ITEM_SEPARATOR() \
    {NULL, NULL, NULL, MENU_ITEM_TYPE_SEPARATOR, 0, NULL, NULL, 0, NULL, NULL}

#define MENU_ITEM_BACK() \
    {"0", "Back", "Return to previous menu", MENU_ITEM_TYPE_BACK, 0, NULL, NULL, 0, NULL, NULL}

#define MENU_ITEM_HIDDEN(key, name, desc, handler) \
    {key, name, desc, MENU_ITEM_TYPE_COMMAND, MENU_FLAG_HIDDEN, handler, NULL, 0, NULL, NULL}

#define MENU_ITEM_CONFIRM(key, name, desc, handler) \
    {key, name, desc, MENU_ITEM_TYPE_COMMAND, MENU_FLAG_CONFIRM, handler, NULL, 0, NULL, NULL}

#define MENU_TABLE(name) \
    static const menu_item_t name[]

#define MENU_TABLE_END \
    {NULL, NULL, NULL, MENU_ITEM_TYPE_COMMAND, 0, NULL, NULL, 0, NULL, NULL}

#endif
