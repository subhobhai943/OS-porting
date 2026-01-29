/**
 * AAAos Kernel - Real-Time Clock (RTC) Driver Implementation
 *
 * Implements CMOS RTC access for date/time operations.
 * Handles BCD/binary conversion and 12/24 hour mode.
 */

#include "rtc.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"

/* Static configuration detected during init */
static bool rtc_is_binary_mode = false;     /* true if RTC uses binary, false for BCD */
static bool rtc_is_24hour_mode = true;      /* true if 24-hour format */
static bool rtc_initialized = false;
static uint8_t rtc_century_register = 0;    /* Century register (0 if not available) */

/* Periodic interrupt callback */
static void (*rtc_periodic_callback)(void) = NULL;

/* Days in each month (non-leap year) */
const uint8_t rtc_days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* Day names */
const char* rtc_weekday_names[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

/* Month names */
const char* rtc_month_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

/* Abbreviated day names */
static const char* rtc_weekday_abbrev[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* Abbreviated month names */
static const char* rtc_month_abbrev[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * Convert BCD to binary
 */
static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * Convert binary to BCD
 */
static uint8_t binary_to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

/**
 * Read a CMOS register
 */
uint8_t rtc_read_register(uint8_t reg) {
    /* Write register number to address port (preserve NMI disable bit) */
    outb(RTC_ADDRESS_PORT, reg);
    io_wait();
    /* Read data from data port */
    return inb(RTC_DATA_PORT);
}

/**
 * Write a CMOS register
 */
void rtc_write_register(uint8_t reg, uint8_t value) {
    /* Write register number to address port */
    outb(RTC_ADDRESS_PORT, reg);
    io_wait();
    /* Write data to data port */
    outb(RTC_DATA_PORT, value);
    io_wait();
}

/**
 * Wait for RTC update to complete
 * Returns when Update-In-Progress flag is clear
 */
static void rtc_wait_for_update(void) {
    /* Wait while update is in progress */
    while (rtc_read_register(RTC_REG_STATUS_A) & RTC_SRA_UIP) {
        /* Spin - update takes ~244 microseconds */
    }
}

/**
 * Initialize the RTC driver
 */
int rtc_init(void) {
    uint8_t status_b;

    kprintf("[RTC] Initializing Real-Time Clock driver\n");

    /* Read Status Register B to determine mode */
    status_b = rtc_read_register(RTC_REG_STATUS_B);

    /* Check data mode (BCD vs binary) */
    rtc_is_binary_mode = (status_b & RTC_SRB_DM) != 0;
    kprintf("[RTC] Data mode: %s\n", rtc_is_binary_mode ? "Binary" : "BCD");

    /* Check hour format (12 vs 24 hour) */
    rtc_is_24hour_mode = (status_b & RTC_SRB_24H) != 0;
    kprintf("[RTC] Hour format: %s\n", rtc_is_24hour_mode ? "24-hour" : "12-hour");

    /* Check battery status */
    uint8_t status_d = rtc_read_register(RTC_REG_STATUS_D);
    if (!(status_d & RTC_SRD_VRT)) {
        kprintf("[RTC] WARNING: RTC battery may be low or time invalid!\n");
    }

    /* Try to detect century register (not standardized) */
    /* Common locations: 0x32 (IBM PC), 0x48 (some ACPI systems) */
    rtc_century_register = RTC_REG_CENTURY;

    rtc_initialized = true;

    /* Read and display current time */
    rtc_time_t time;
    if (rtc_get_time(&time) == 0) {
        char buf[32];
        rtc_format_time(&time, buf, sizeof(buf));
        kprintf("[RTC] Current time: %s\n", buf);
    }

    kprintf("[RTC] Initialization complete\n");
    return 0;
}

/**
 * Get current time from RTC
 */
int rtc_get_time(rtc_time_t *time) {
    uint8_t second, minute, hour, day, month, year, weekday;
    uint8_t century = 20;  /* Default to 21st century (2000s) */
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year;

    if (!time) {
        return -1;
    }

    /* Read time values twice and ensure they match to avoid reading during update */
    do {
        /* Wait for any update to complete */
        rtc_wait_for_update();

        /* Read all registers */
        second  = rtc_read_register(RTC_REG_SECONDS);
        minute  = rtc_read_register(RTC_REG_MINUTES);
        hour    = rtc_read_register(RTC_REG_HOURS);
        day     = rtc_read_register(RTC_REG_DAY);
        month   = rtc_read_register(RTC_REG_MONTH);
        year    = rtc_read_register(RTC_REG_YEAR);
        weekday = rtc_read_register(RTC_REG_WEEKDAY);

        /* Try to read century if available */
        if (rtc_century_register != 0) {
            century = rtc_read_register(rtc_century_register);
        }

        /* Wait and read again */
        rtc_wait_for_update();

        last_second = rtc_read_register(RTC_REG_SECONDS);
        last_minute = rtc_read_register(RTC_REG_MINUTES);
        last_hour   = rtc_read_register(RTC_REG_HOURS);
        last_day    = rtc_read_register(RTC_REG_DAY);
        last_month  = rtc_read_register(RTC_REG_MONTH);
        last_year   = rtc_read_register(RTC_REG_YEAR);

    } while (second != last_second || minute != last_minute ||
             hour != last_hour || day != last_day ||
             month != last_month || year != last_year);

    /* Convert from BCD if necessary */
    if (!rtc_is_binary_mode) {
        second  = bcd_to_binary(second);
        minute  = bcd_to_binary(minute);
        day     = bcd_to_binary(day);
        month   = bcd_to_binary(month);
        year    = bcd_to_binary(year);
        weekday = bcd_to_binary(weekday);

        if (rtc_century_register != 0) {
            century = bcd_to_binary(century);
        }

        /* Handle hour separately due to 12-hour mode complexity */
        if (!rtc_is_24hour_mode) {
            bool pm = (hour & 0x80) != 0;
            hour = bcd_to_binary(hour & 0x7F);
            if (pm && hour != 12) {
                hour += 12;
            } else if (!pm && hour == 12) {
                hour = 0;
            }
        } else {
            hour = bcd_to_binary(hour);
        }
    } else {
        /* Binary mode - still need to handle 12-hour format */
        if (!rtc_is_24hour_mode) {
            bool pm = (hour & 0x80) != 0;
            hour = hour & 0x7F;
            if (pm && hour != 12) {
                hour += 12;
            } else if (!pm && hour == 12) {
                hour = 0;
            }
        }
    }

    /* Calculate full year */
    if (rtc_century_register != 0 && century >= 19 && century <= 21) {
        time->year = century * 100 + year;
    } else {
        /* Assume 2000s if century not available or invalid */
        time->year = 2000 + year;
        /* Handle potential Y2K style wraparound */
        if (year > 80) {
            time->year = 1900 + year;
        }
    }

    time->month   = month;
    time->day     = day;
    time->hour    = hour;
    time->minute  = minute;
    time->second  = second;
    time->weekday = weekday;

    /* Validate weekday (some BIOSes don't set it correctly) */
    if (time->weekday < 1 || time->weekday > 7) {
        time->weekday = rtc_calculate_weekday(time->year, time->month, time->day);
    }

    return 0;
}

/**
 * Set RTC time
 */
int rtc_set_time(const rtc_time_t *time) {
    uint8_t second, minute, hour, day, month, year, weekday;
    uint8_t century;
    uint8_t status_b;

    if (!time) {
        return -1;
    }

    /* Validate input */
    if (time->month < 1 || time->month > 12 ||
        time->day < 1 || time->day > 31 ||
        time->hour > 23 || time->minute > 59 || time->second > 59) {
        kprintf("[RTC] ERROR: Invalid time values\n");
        return -1;
    }

    kprintf("[RTC] Setting time: %04u-%02u-%02u %02u:%02u:%02u\n",
            time->year, time->month, time->day,
            time->hour, time->minute, time->second);

    /* Prepare values */
    second  = time->second;
    minute  = time->minute;
    hour    = time->hour;
    day     = time->day;
    month   = time->month;
    year    = time->year % 100;
    century = time->year / 100;
    weekday = time->weekday;

    /* Calculate weekday if not provided */
    if (weekday < 1 || weekday > 7) {
        weekday = rtc_calculate_weekday(time->year, time->month, time->day);
    }

    /* Handle 12-hour mode */
    if (!rtc_is_24hour_mode) {
        bool pm = hour >= 12;
        if (hour == 0) {
            hour = 12;
        } else if (hour > 12) {
            hour -= 12;
        }
        if (pm) {
            hour |= 0x80;
        }
    }

    /* Convert to BCD if necessary */
    if (!rtc_is_binary_mode) {
        second  = binary_to_bcd(second);
        minute  = binary_to_bcd(minute);
        hour    = (hour & 0x80) | binary_to_bcd(hour & 0x7F);
        day     = binary_to_bcd(day);
        month   = binary_to_bcd(month);
        year    = binary_to_bcd(year);
        century = binary_to_bcd(century);
        weekday = binary_to_bcd(weekday);
    }

    /* Disable RTC updates during write */
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    rtc_write_register(RTC_REG_STATUS_B, status_b | RTC_SRB_SET);

    /* Write all registers */
    rtc_write_register(RTC_REG_SECONDS, second);
    rtc_write_register(RTC_REG_MINUTES, minute);
    rtc_write_register(RTC_REG_HOURS, hour);
    rtc_write_register(RTC_REG_DAY, day);
    rtc_write_register(RTC_REG_MONTH, month);
    rtc_write_register(RTC_REG_YEAR, year);
    rtc_write_register(RTC_REG_WEEKDAY, weekday);

    /* Write century if available */
    if (rtc_century_register != 0) {
        rtc_write_register(rtc_century_register, century);
    }

    /* Re-enable RTC updates */
    rtc_write_register(RTC_REG_STATUS_B, status_b & ~RTC_SRB_SET);

    kprintf("[RTC] Time set successfully\n");
    return 0;
}

/**
 * Check if year is a leap year
 */
bool rtc_is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * Get days in a month
 */
uint8_t rtc_days_in_month_func(uint8_t month, uint16_t year) {
    if (month < 1 || month > 12) {
        return 0;
    }

    if (month == 2 && rtc_is_leap_year(year)) {
        return 29;
    }

    return rtc_days_in_month[month - 1];
}

/**
 * Calculate day of week using Zeller's congruence (modified for Sunday=1)
 */
uint8_t rtc_calculate_weekday(uint16_t year, uint8_t month, uint8_t day) {
    int y = year;
    int m = month;
    int d = day;

    /* Adjust for Zeller's formula (January and February are months 13 and 14) */
    if (m < 3) {
        m += 12;
        y--;
    }

    int k = y % 100;
    int j = y / 100;

    /* Zeller's congruence */
    int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    /* Convert to Sunday=1 format (Zeller gives Saturday=0) */
    int weekday = ((h + 6) % 7) + 1;

    return (uint8_t)weekday;
}

/**
 * Convert rtc_time_t to Unix timestamp
 */
uint64_t rtc_time_to_unix(const rtc_time_t *time) {
    uint64_t timestamp = 0;
    uint16_t year;

    if (!time || time->year < 1970) {
        return 0;
    }

    /* Count days from 1970 to the given year */
    for (year = 1970; year < time->year; year++) {
        timestamp += rtc_is_leap_year(year) ? 366 : 365;
    }

    /* Add days for each month in the current year */
    for (uint8_t month = 1; month < time->month; month++) {
        timestamp += rtc_days_in_month_func(month, time->year);
    }

    /* Add days in current month (minus 1 since day 1 is start of day) */
    timestamp += time->day - 1;

    /* Convert days to seconds and add time */
    timestamp = timestamp * 24 * 60 * 60;
    timestamp += time->hour * 60 * 60;
    timestamp += time->minute * 60;
    timestamp += time->second;

    return timestamp;
}

/**
 * Convert Unix timestamp to rtc_time_t
 */
void rtc_unix_to_time(uint64_t timestamp, rtc_time_t *time) {
    uint64_t days, seconds_in_day;
    uint16_t year;
    uint8_t month;

    if (!time) {
        return;
    }

    /* Extract time of day */
    days = timestamp / (24 * 60 * 60);
    seconds_in_day = timestamp % (24 * 60 * 60);

    time->hour   = seconds_in_day / 3600;
    time->minute = (seconds_in_day % 3600) / 60;
    time->second = seconds_in_day % 60;

    /* Calculate year */
    year = 1970;
    while (days >= (rtc_is_leap_year(year) ? 366 : 365)) {
        days -= rtc_is_leap_year(year) ? 366 : 365;
        year++;
    }
    time->year = year;

    /* Calculate month and day */
    month = 1;
    while (days >= rtc_days_in_month_func(month, year)) {
        days -= rtc_days_in_month_func(month, year);
        month++;
    }
    time->month = month;
    time->day = days + 1;

    /* Calculate weekday */
    time->weekday = rtc_calculate_weekday(time->year, time->month, time->day);
}

/**
 * Get Unix timestamp from current RTC time
 */
uint64_t rtc_get_unix_timestamp(void) {
    rtc_time_t time;

    if (rtc_get_time(&time) != 0) {
        return 0;
    }

    return rtc_time_to_unix(&time);
}

/**
 * Helper: copy string to buffer with bounds checking
 */
static int str_copy(char *dst, size_t *pos, size_t max, const char *src) {
    while (*src && *pos < max - 1) {
        dst[(*pos)++] = *src++;
    }
    return 0;
}

/**
 * Helper: format number with leading zeros
 */
static int format_number(char *dst, size_t *pos, size_t max, uint32_t num, int width) {
    char buf[12];
    int i = 0;
    int len;

    /* Convert to string (reverse order) */
    do {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    } while (num > 0 && i < 11);

    len = i;

    /* Add leading zeros */
    while (len < width && *pos < max - 1) {
        dst[(*pos)++] = '0';
        len++;
    }

    /* Copy digits in correct order */
    while (i > 0 && *pos < max - 1) {
        dst[(*pos)++] = buf[--i];
    }

    return 0;
}

/**
 * Format time as ISO 8601 string
 */
int rtc_format_time(const rtc_time_t *time, char *buf, size_t max) {
    return rtc_format_time_custom(time, buf, max, "%Y-%m-%d %H:%M:%S");
}

/**
 * Format time with custom format string
 */
int rtc_format_time_custom(const rtc_time_t *time, char *buf, size_t max, const char *fmt) {
    size_t pos = 0;

    if (!time || !buf || max == 0 || !fmt) {
        return -1;
    }

    while (*fmt && pos < max - 1) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            switch (*fmt) {
                case 'Y':  /* 4-digit year */
                    format_number(buf, &pos, max, time->year, 4);
                    break;

                case 'm':  /* 2-digit month */
                    format_number(buf, &pos, max, time->month, 2);
                    break;

                case 'd':  /* 2-digit day */
                    format_number(buf, &pos, max, time->day, 2);
                    break;

                case 'H':  /* 2-digit hour (24-hour) */
                    format_number(buf, &pos, max, time->hour, 2);
                    break;

                case 'M':  /* 2-digit minute */
                    format_number(buf, &pos, max, time->minute, 2);
                    break;

                case 'S':  /* 2-digit second */
                    format_number(buf, &pos, max, time->second, 2);
                    break;

                case 'a':  /* Abbreviated weekday */
                    if (time->weekday >= 1 && time->weekday <= 7) {
                        str_copy(buf, &pos, max, rtc_weekday_abbrev[time->weekday - 1]);
                    }
                    break;

                case 'A':  /* Full weekday */
                    if (time->weekday >= 1 && time->weekday <= 7) {
                        str_copy(buf, &pos, max, rtc_weekday_names[time->weekday - 1]);
                    }
                    break;

                case 'b':  /* Abbreviated month */
                    if (time->month >= 1 && time->month <= 12) {
                        str_copy(buf, &pos, max, rtc_month_abbrev[time->month - 1]);
                    }
                    break;

                case 'B':  /* Full month */
                    if (time->month >= 1 && time->month <= 12) {
                        str_copy(buf, &pos, max, rtc_month_names[time->month - 1]);
                    }
                    break;

                case '%':  /* Literal percent */
                    buf[pos++] = '%';
                    break;

                default:
                    /* Unknown format, copy as-is */
                    buf[pos++] = '%';
                    if (pos < max - 1) {
                        buf[pos++] = *fmt;
                    }
                    break;
            }
            fmt++;
        } else {
            buf[pos++] = *fmt++;
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

/**
 * Enable periodic interrupts
 */
int rtc_enable_periodic_interrupt(uint8_t rate) {
    uint8_t status_a, status_b;

    /* Rate must be between 3 and 15 */
    if (rate < 3 || rate > 15) {
        kprintf("[RTC] ERROR: Invalid interrupt rate %u (must be 3-15)\n", rate);
        return -1;
    }

    kprintf("[RTC] Enabling periodic interrupt at rate %u\n", rate);

    /* Set rate in Status Register A */
    status_a = rtc_read_register(RTC_REG_STATUS_A);
    status_a = (status_a & 0xF0) | rate;
    rtc_write_register(RTC_REG_STATUS_A, status_a);

    /* Enable periodic interrupt in Status Register B */
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    rtc_write_register(RTC_REG_STATUS_B, status_b | RTC_SRB_PIE);

    /* Read Status Register C to clear any pending interrupt */
    rtc_read_register(RTC_REG_STATUS_C);

    return 0;
}

/**
 * Disable periodic interrupts
 */
int rtc_disable_periodic_interrupt(void) {
    uint8_t status_b;

    kprintf("[RTC] Disabling periodic interrupt\n");

    /* Disable periodic interrupt in Status Register B */
    status_b = rtc_read_register(RTC_REG_STATUS_B);
    rtc_write_register(RTC_REG_STATUS_B, status_b & ~RTC_SRB_PIE);

    /* Read Status Register C to clear any pending interrupt */
    rtc_read_register(RTC_REG_STATUS_C);

    return 0;
}

/**
 * RTC interrupt handler
 */
void rtc_interrupt_handler(void *frame) {
    uint8_t status_c;

    (void)frame;  /* Unused */

    /* Read Status Register C to acknowledge interrupt and determine source */
    status_c = rtc_read_register(RTC_REG_STATUS_C);

    /* Check if this was a periodic interrupt */
    if (status_c & RTC_SRC_PF) {
        /* Call callback if registered */
        if (rtc_periodic_callback) {
            rtc_periodic_callback();
        }
    }

    /* Check for alarm interrupt */
    if (status_c & RTC_SRC_AF) {
        kprintf("[RTC] Alarm triggered!\n");
    }

    /* Check for update-ended interrupt */
    if (status_c & RTC_SRC_UF) {
        /* Update ended - could be used for second-accurate timing */
    }
}
