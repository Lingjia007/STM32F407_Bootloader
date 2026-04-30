#ifndef PLATFORM_RTC_H
#define PLATFORM_RTC_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum
{
    RTC_STATUS_OK = 0,
    RTC_STATUS_ERROR,
    RTC_STATUS_INVALID_PARAM,
    RTC_STATUS_NOT_INITIALIZED
} platform_rtc_status_t;

typedef struct
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t subseconds;
} platform_rtc_time_t;

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t date;
    uint8_t weekday;
} platform_rtc_date_t;

typedef struct
{
    int16_t (*init)(void *ctx);
    int16_t (*deinit)(void *ctx);
    int16_t (*get_time)(void *ctx, platform_rtc_time_t *time);
    int16_t (*get_date)(void *ctx, platform_rtc_date_t *date);
    int16_t (*set_time)(void *ctx, const platform_rtc_time_t *time);
    int16_t (*set_date)(void *ctx, const platform_rtc_date_t *date);
    int16_t (*get_timestamp)(void *ctx, uint32_t *timestamp);
    int16_t (*set_timestamp)(void *ctx, uint32_t timestamp);
} platform_rtc_ops_t;

typedef struct
{
    const platform_rtc_ops_t *ops;
    const char *name;
    void *user_data;
} platform_rtc_base_t;

#define RTC_INIT(rtc) \
    ((rtc) && (rtc)->ops && (rtc)->ops->init ? (rtc)->ops->init((rtc)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_DEINIT(rtc) \
    ((rtc) && (rtc)->ops && (rtc)->ops->deinit ? (rtc)->ops->deinit((rtc)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_GET_TIME(rtc, time) \
    ((rtc) && (rtc)->ops && (rtc)->ops->get_time ? (rtc)->ops->get_time((rtc), (time)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_GET_DATE(rtc, date) \
    ((rtc) && (rtc)->ops && (rtc)->ops->get_date ? (rtc)->ops->get_date((rtc), (date)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_SET_TIME(rtc, time) \
    ((rtc) && (rtc)->ops && (rtc)->ops->set_time ? (rtc)->ops->set_time((rtc), (time)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_SET_DATE(rtc, date) \
    ((rtc) && (rtc)->ops && (rtc)->ops->set_date ? (rtc)->ops->set_date((rtc), (date)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_GET_TIMESTAMP(rtc, ts) \
    ((rtc) && (rtc)->ops && (rtc)->ops->get_timestamp ? (rtc)->ops->get_timestamp((rtc), (ts)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_SET_TIMESTAMP(rtc, ts) \
    ((rtc) && (rtc)->ops && (rtc)->ops->set_timestamp ? (rtc)->ops->set_timestamp((rtc), (ts)) : (int16_t)RTC_STATUS_ERROR)

#define RTC_INIT_BASE(rtc_ptr, ops_ptr, rtc_name) \
    do \
    { \
        (rtc_ptr)->ops = (ops_ptr); \
        (rtc_ptr)->name = (rtc_name); \
        (rtc_ptr)->user_data = NULL; \
    } while (0)

uint32_t platform_rtc_datetime_to_timestamp(const platform_rtc_date_t *date, const platform_rtc_time_t *time);
void platform_rtc_timestamp_to_datetime(uint32_t timestamp, platform_rtc_date_t *date, platform_rtc_time_t *time);

#endif
