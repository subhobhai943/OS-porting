/**
 * AAAos Kernel - Real-Time Clock (RTC) Driver
 *
 * The CMOS Real-Time Clock provides persistent date/time tracking:
 * - Battery-backed time keeping (persists across power cycles)
 * - Current date and time (seconds, minutes, hours, day, month, year)
 * - Optional periodic interrupts on IRQ8
 * - Alarm functionality
 *
 * CMOS RTC Ports:
 * - 0x70: Address/Index port (write register number)
 * - 0x71: Data port (read/write register data)
 *
 * The RTC uses either BCD or binary format depending on Status Register B.
 * Time can be in 12-hour or 24-hour format.
 */

#ifndef _AAAOS_DRIVERS_RTC_H
#define _AAAOS_DRIVERS_RTC_H

#include "../../kernel/include/types.h"

/* CMOS RTC I/O Ports */
#define RTC_ADDRESS_PORT    0x70    /* Address/Index port */
#define RTC_DATA_PORT       0x71    /* Data port */

/* CMOS RTC Registers */
#define RTC_REG_SECONDS     0x00    /* Seconds (0-59) */
#define RTC_REG_MINUTES     0x02    /* Minutes (0-59) */
#define RTC_REG_HOURS       0x04    /* Hours (0-23 or 1-12 with AM/PM) */
#define RTC_REG_WEEKDAY     0x06    /* Day of week (1-7, Sunday=1) */
#define RTC_REG_DAY         0x07    /* Day of month (1-31) */
#define RTC_REG_MONTH       0x08    /* Month (1-12) */
#define RTC_REG_YEAR        0x09    /* Year (0-99) */
#define RTC_REG_STATUS_A    0x0A    /* Status Register A */
#define RTC_REG_STATUS_B    0x0B    /* Status Register B */
#define RTC_REG_STATUS_C    0x0C    /* Status Register C (read to acknowledge IRQ) */
#define RTC_REG_STATUS_D    0x0D    /* Status Register D */
#define RTC_REG_CENTURY     0x32    /* Century register (not always available) */

/* Status Register A bits */
#define RTC_SRA_UIP         0x80    /* Update In Progress - don't read time when set */
#define RTC_SRA_DV_MASK     0x70    /* Divider bits (usually 010 for 32.768kHz) */
#define RTC_SRA_RS_MASK     0x0F    /* Rate selection bits for periodic interrupt */

/* Status Register B bits */
#define RTC_SRB_SET         0x80    /* SET: 1=stop updates, 0=normal operation */
#define RTC_SRB_PIE         0x40    /* Periodic Interrupt Enable */
#define RTC_SRB_AIE         0x20    /* Alarm Interrupt Enable */
#define RTC_SRB_UIE         0x10    /* Update-ended Interrupt Enable */
#define RTC_SRB_SQWE        0x08    /* Square Wave Enable */
#define RTC_SRB_DM          0x04    /* Data Mode: 1=binary, 0=BCD */
#define RTC_SRB_24H         0x02    /* Hour format: 1=24-hour, 0=12-hour */
#define RTC_SRB_DSE         0x01    /* Daylight Savings Enable */

/* Status Register C bits (read to clear, acknowledge interrupt) */
#define RTC_SRC_IRQF        0x80    /* Interrupt Request Flag */
#define RTC_SRC_PF          0x40    /* Periodic interrupt Flag */
#define RTC_SRC_AF          0x20    /* Alarm Flag */
#define RTC_SRC_UF          0x10    /* Update-ended Flag */

/* Status Register D bits */
#define RTC_SRD_VRT         0x80    /* Valid RAM and Time (battery OK) */

/* NMI disable bit (bit 7 of address port) */
#define RTC_NMI_DISABLE     0x80

/* RTC IRQ number */
#define RTC_IRQ             8

/* Days in each month (non-leap year) */
extern const uint8_t rtc_days_in_month[12];

/* Day names */
extern const char* rtc_weekday_names[7];

/* Month names */
extern const char* rtc_month_names[12];

/**
 * RTC time structure
 *
 * Holds complete date and time information.
 * Year is stored as full year (e.g., 2024).
 * Weekday: 1=Sunday, 2=Monday, ..., 7=Saturday
 */
typedef struct {
    uint16_t year;      /* Full year (e.g., 2024) */
    uint8_t  month;     /* Month (1-12) */
    uint8_t  day;       /* Day of month (1-31) */
    uint8_t  hour;      /* Hour (0-23) */
    uint8_t  minute;    /* Minute (0-59) */
    uint8_t  second;    /* Second (0-59) */
    uint8_t  weekday;   /* Day of week (1-7, 1=Sunday) */
} rtc_time_t;

/**
 * Initialize the Real-Time Clock driver
 *
 * Reads RTC configuration to determine BCD/binary and 12/24 hour modes.
 * Optionally enables RTC interrupts if needed.
 *
 * @return 0 on success, negative on error
 */
int rtc_init(void);

/**
 * Get current date and time from RTC
 *
 * Reads all time registers from CMOS RTC. Waits for update-in-progress
 * flag to clear before reading to ensure consistent values.
 * Handles BCD to binary conversion if needed.
 *
 * @param time Pointer to rtc_time_t structure to fill
 * @return 0 on success, negative on error
 */
int rtc_get_time(rtc_time_t *time);

/**
 * Set the RTC date and time
 *
 * Writes all time registers to CMOS RTC. Temporarily halts RTC updates
 * during write to ensure consistency.
 * Handles binary to BCD conversion if needed.
 *
 * @param time Pointer to rtc_time_t structure with new time
 * @return 0 on success, negative on error
 */
int rtc_set_time(const rtc_time_t *time);

/**
 * Get Unix timestamp (seconds since 1970-01-01 00:00:00 UTC)
 *
 * Reads current RTC time and converts to Unix timestamp.
 * Note: RTC is typically in local time, not UTC.
 *
 * @return Unix timestamp, or 0 on error
 */
uint64_t rtc_get_unix_timestamp(void);

/**
 * Convert rtc_time_t to Unix timestamp
 *
 * @param time Pointer to rtc_time_t structure
 * @return Unix timestamp
 */
uint64_t rtc_time_to_unix(const rtc_time_t *time);

/**
 * Convert Unix timestamp to rtc_time_t
 *
 * @param timestamp Unix timestamp
 * @param time Pointer to rtc_time_t structure to fill
 */
void rtc_unix_to_time(uint64_t timestamp, rtc_time_t *time);

/**
 * Format time as string
 *
 * Formats time in ISO 8601 format: "YYYY-MM-DD HH:MM:SS"
 *
 * @param time Pointer to rtc_time_t structure
 * @param buf Output buffer
 * @param max Maximum buffer size
 * @return Number of characters written (excluding null), negative on error
 */
int rtc_format_time(const rtc_time_t *time, char *buf, size_t max);

/**
 * Format time with custom format string
 *
 * Format specifiers:
 *   %Y - 4-digit year
 *   %m - 2-digit month (01-12)
 *   %d - 2-digit day (01-31)
 *   %H - 2-digit hour (00-23)
 *   %M - 2-digit minute (00-59)
 *   %S - 2-digit second (00-59)
 *   %a - Abbreviated weekday name (Sun, Mon, ...)
 *   %A - Full weekday name (Sunday, Monday, ...)
 *   %b - Abbreviated month name (Jan, Feb, ...)
 *   %B - Full month name (January, February, ...)
 *   %% - Literal percent sign
 *
 * @param time Pointer to rtc_time_t structure
 * @param buf Output buffer
 * @param max Maximum buffer size
 * @param fmt Format string
 * @return Number of characters written (excluding null), negative on error
 */
int rtc_format_time_custom(const rtc_time_t *time, char *buf, size_t max, const char *fmt);

/**
 * Check if year is a leap year
 *
 * @param year Full year (e.g., 2024)
 * @return true if leap year, false otherwise
 */
bool rtc_is_leap_year(uint16_t year);

/**
 * Get number of days in a month
 *
 * @param month Month (1-12)
 * @param year Full year (for leap year check in February)
 * @return Number of days in the month
 */
uint8_t rtc_days_in_month_func(uint8_t month, uint16_t year);

/**
 * Calculate day of week from date
 *
 * Uses Zeller's congruence algorithm.
 *
 * @param year Full year
 * @param month Month (1-12)
 * @param day Day of month (1-31)
 * @return Day of week (1-7, 1=Sunday)
 */
uint8_t rtc_calculate_weekday(uint16_t year, uint8_t month, uint8_t day);

/**
 * Enable RTC periodic interrupts
 *
 * Configures RTC to generate periodic interrupts at the specified rate.
 * Rate is 32768 >> (rate-1) Hz, where rate is 3-15.
 * Rate 6 = 1024 Hz, Rate 15 = 2 Hz
 *
 * @param rate Rate selection (3-15)
 * @return 0 on success, negative on error
 */
int rtc_enable_periodic_interrupt(uint8_t rate);

/**
 * Disable RTC periodic interrupts
 *
 * @return 0 on success, negative on error
 */
int rtc_disable_periodic_interrupt(void);

/**
 * RTC interrupt handler
 *
 * Called when RTC interrupt (IRQ8) fires.
 * Reads Status Register C to acknowledge interrupt.
 *
 * @param frame Interrupt frame (unused)
 */
void rtc_interrupt_handler(void *frame);

/**
 * Read raw CMOS register
 *
 * Low-level function to read a CMOS register directly.
 *
 * @param reg Register number (0x00-0x7F)
 * @return Register value
 */
uint8_t rtc_read_register(uint8_t reg);

/**
 * Write raw CMOS register
 *
 * Low-level function to write a CMOS register directly.
 *
 * @param reg Register number (0x00-0x7F)
 * @param value Value to write
 */
void rtc_write_register(uint8_t reg, uint8_t value);

#endif /* _AAAOS_DRIVERS_RTC_H */
