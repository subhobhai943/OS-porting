#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

void serial_init(unsigned short port) {
    (void)port;
}

void serial_putc(unsigned short port, char c) {
    (void)port;
    putchar(c);
}

void serial_puts(unsigned short port, const char *str) {
    (void)port;
    fputs(str, stdout);
}

void serial_printf(unsigned short port, const char *fmt, ...) {
    va_list args;
    (void)port;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

char serial_getc(unsigned short port) {
    (void)port;
    return (char)getchar();
}

bool serial_data_ready(unsigned short port) {
    (void)port;
    return false;
}
