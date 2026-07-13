#include "bignum128.h"

u128 u128_two_pow_64(void) {
    return ((u128)1) << 64;
}

bool u128_parse_decimal(const char *digits, size_t len, u128 *out) {
    if (len == 0) return false;
    u128 v = 0;
    for (size_t i = 0; i < len; i++) {
        char c = digits[i];
        if (c < '0' || c > '9') return false;
        unsigned d = (unsigned)(c - '0');
        /* Overflow check before the multiply-add, since u128 arithmetic
         * silently wraps (it's an unsigned type) rather than trapping. */
        if (v > (U128_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

void u128_to_minimal_be_bytes(u128 v, ByteBuf *out) {
    uint8_t buf[16];
    for (int i = 0; i < 16; i++) {
        buf[15 - i] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
    size_t start = 0;
    while (start < 15 && buf[start] == 0) start++;
    bytebuf_append(out, buf + start, 16 - start);
}

void u128_to_decimal_string(u128 v, char *buf, size_t buf_size) {
    char tmp[40];
    size_t i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0) {
            tmp[i++] = (char)('0' + (int)(v % 10));
            v /= 10;
        }
    }
    size_t n = i < buf_size - 1 ? i : buf_size - 1;
    for (size_t k = 0; k < n; k++) {
        buf[k] = tmp[i - 1 - k];
    }
    buf[n] = '\0';
}
