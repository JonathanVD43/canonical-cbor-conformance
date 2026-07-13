#include "adapter_error.h"

#include <stdarg.h>
#include <stdio.h>

static char g_last_error[512] = "";

void adapter_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof g_last_error, fmt, ap);
    va_end(ap);
}

const char *adapter_last_error(void) {
    return g_last_error;
}
