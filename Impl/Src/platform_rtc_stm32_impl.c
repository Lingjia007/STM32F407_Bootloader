#include "platform_rtc_stm32_impl.h"
#include <string.h>

static int16_t rtc_stm32_init(void *ctx)
{
    rtc_stm32_t *self = container_of(ctx, rtc_stm32_t, base);
    
    if (self->hrtc == NULL)
    {
        return RTC_STATUS_INVALID_PARAM;
    }
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_deinit(void *ctx)
{
    (void)ctx;
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_get_time(void *ctx, platform_rtc_time_t *time)
{
    rtc_stm32_t *self = container_of(ctx, rtc_stm32_t, base);
    RTC_TimeTypeDef hal_time;
    
    if (time == NULL || self->hrtc == NULL)
    {
        return RTC_STATUS_INVALID_PARAM;
    }
    
    if (HAL_RTC_GetTime(self->hrtc, &hal_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RTC_STATUS_ERROR;
    }
    
    time->hours = hal_time.Hours;
    time->minutes = hal_time.Minutes;
    time->seconds = hal_time.Seconds;
    time->subseconds = hal_time.SubSeconds;
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_get_date(void *ctx, platform_rtc_date_t *date)
{
    rtc_stm32_t *self = container_of(ctx, rtc_stm32_t, base);
    RTC_DateTypeDef hal_date;
    
    if (date == NULL || self->hrtc == NULL)
    {
        return RTC_STATUS_INVALID_PARAM;
    }
    
    if (HAL_RTC_GetDate(self->hrtc, &hal_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RTC_STATUS_ERROR;
    }
    
    date->year = hal_date.Year + 2000;
    date->month = hal_date.Month;
    date->date = hal_date.Date;
    date->weekday = hal_date.WeekDay;
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_set_time(void *ctx, const platform_rtc_time_t *time)
{
    rtc_stm32_t *self = container_of(ctx, rtc_stm32_t, base);
    RTC_TimeTypeDef hal_time;
    
    if (time == NULL || self->hrtc == NULL)
    {
        return RTC_STATUS_INVALID_PARAM;
    }
    
    memset(&hal_time, 0, sizeof(hal_time));
    hal_time.Hours = time->hours;
    hal_time.Minutes = time->minutes;
    hal_time.Seconds = time->seconds;
    hal_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    hal_time.StoreOperation = RTC_STOREOPERATION_RESET;
    
    if (HAL_RTC_SetTime(self->hrtc, &hal_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RTC_STATUS_ERROR;
    }
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_set_date(void *ctx, const platform_rtc_date_t *date)
{
    rtc_stm32_t *self = container_of(ctx, rtc_stm32_t, base);
    RTC_DateTypeDef hal_date;
    
    if (date == NULL || self->hrtc == NULL)
    {
        return RTC_STATUS_INVALID_PARAM;
    }
    
    memset(&hal_date, 0, sizeof(hal_date));
    hal_date.Year = date->year - 2000;
    hal_date.Month = date->month;
    hal_date.Date = date->date;
    hal_date.WeekDay = date->weekday;
    
    if (HAL_RTC_SetDate(self->hrtc, &hal_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RTC_STATUS_ERROR;
    }
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_get_timestamp(void *ctx, uint32_t *timestamp)
{
    platform_rtc_time_t time;
    platform_rtc_date_t date;
    int16_t ret;
    
    ret = rtc_stm32_get_time(ctx, &time);
    if (ret != RTC_STATUS_OK)
    {
        return ret;
    }
    
    ret = rtc_stm32_get_date(ctx, &date);
    if (ret != RTC_STATUS_OK)
    {
        return ret;
    }
    
    *timestamp = platform_rtc_datetime_to_timestamp(&date, &time);
    
    return RTC_STATUS_OK;
}

static int16_t rtc_stm32_set_timestamp(void *ctx, uint32_t timestamp)
{
    platform_rtc_time_t time;
    platform_rtc_date_t date;
    int16_t ret;
    
    platform_rtc_timestamp_to_datetime(timestamp, &date, &time);
    
    ret = rtc_stm32_set_date(ctx, &date);
    if (ret != RTC_STATUS_OK)
    {
        return ret;
    }
    
    ret = rtc_stm32_set_time(ctx, &time);
    if (ret != RTC_STATUS_OK)
    {
        return ret;
    }
    
    return RTC_STATUS_OK;
}

static const platform_rtc_ops_t rtc_stm32_ops = {
    .init = rtc_stm32_init,
    .deinit = rtc_stm32_deinit,
    .get_time = rtc_stm32_get_time,
    .get_date = rtc_stm32_get_date,
    .set_time = rtc_stm32_set_time,
    .set_date = rtc_stm32_set_date,
    .get_timestamp = rtc_stm32_get_timestamp,
    .set_timestamp = rtc_stm32_set_timestamp,
};

void platform_rtc_stm32_register(rtc_stm32_t *rtc, RTC_HandleTypeDef *hrtc, const char *name)
{
    if (rtc == NULL || hrtc == NULL)
    {
        return;
    }
    
    rtc->hrtc = hrtc;
    RTC_INIT_BASE(&rtc->base, &rtc_stm32_ops, name);
}

uint32_t platform_rtc_datetime_to_timestamp(const platform_rtc_date_t *date, const platform_rtc_time_t *time)
{
    if (date == NULL || time == NULL)
    {
        return 0;
    }
    
    uint16_t year = date->year;
    uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
    
    static const uint16_t days_before_month[2][12] = {
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
        {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}
    };
    
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; y++)
    {
        days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
    }
    
    if (date->month >= 1 && date->month <= 12)
    {
        days += days_before_month[is_leap][date->month - 1];
    }
    days += date->date - 1;
    
    return days * 86400UL + time->hours * 3600UL + time->minutes * 60UL + time->seconds;
}

void platform_rtc_timestamp_to_datetime(uint32_t timestamp, platform_rtc_date_t *date, platform_rtc_time_t *time)
{
    if (date == NULL || time == NULL)
    {
        return;
    }
    
    uint32_t seconds = timestamp;
    uint32_t days = seconds / 86400UL;
    seconds = seconds % 86400UL;
    
    time->hours = (uint8_t)(seconds / 3600UL);
    seconds = seconds % 3600UL;
    time->minutes = (uint8_t)(seconds / 60UL);
    time->seconds = (uint8_t)(seconds % 60UL);
    time->subseconds = 0;
    
    uint16_t year = 1970;
    while (1)
    {
        uint16_t days_in_year = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
        if (days < days_in_year)
        {
            break;
        }
        days -= days_in_year;
        year++;
    }
    
    date->year = year;
    
    uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
    static const uint8_t days_in_month[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    };
    
    uint8_t month = 1;
    while (month <= 12)
    {
        if (days < days_in_month[is_leap][month - 1])
        {
            break;
        }
        days -= days_in_month[is_leap][month - 1];
        month++;
    }
    
    date->month = month;
    date->date = (uint8_t)(days + 1);
    date->weekday = 1;
}
