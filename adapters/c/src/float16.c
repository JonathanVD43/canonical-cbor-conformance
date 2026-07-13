#include "float16.h"

#include <string.h>

static uint64_t double_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    return u;
}

static double bits_double(uint64_t u) {
    double v;
    memcpy(&v, &u, sizeof(v));
    return v;
}

static uint32_t float_bits(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    return u;
}

uint16_t double_to_f16_bits(double value) {
    uint64_t x = double_bits(value);
    uint64_t sign = (x >> 63) & 1;
    uint64_t exp = (x >> 52) & 0x7ff;
    uint64_t man = x & 0xfffffffffffffULL;

    if (exp == 0x7ff) {
        /* Infinity or NaN. */
        uint64_t new_man = man >> 42;
        uint64_t nan_bit = man != 0 ? (1ULL << 9) : 0;
        return (uint16_t)((sign << 15 | 0x7c00 | new_man | nan_bit) & 0xffff);
    }

    uint64_t half_sign_bits = sign << 15;
    int64_t half_exp = (int64_t)exp - 1023 + 15;

    if (half_exp >= 0x1f) {
        return (uint16_t)((half_sign_bits | 0x7c00) & 0xffff);
    }

    if (half_exp <= 0) {
        if (10 - half_exp > 21) {
            /* Underflows even a subnormal half: signed zero. */
            return (uint16_t)(half_sign_bits & 0xffff);
        }
        uint64_t man_with_hidden = man | 0x10000000000000ULL;
        uint64_t half_man = man_with_hidden >> (uint64_t)(43 - half_exp);
        uint64_t round_bit = ((uint64_t)1) << (uint64_t)(42 - half_exp);
        if ((man_with_hidden & round_bit) != 0 && (man_with_hidden & (3 * round_bit - 1)) != 0) {
            half_man += 1;
        }
        return (uint16_t)((half_sign_bits | half_man) & 0xffff);
    }

    uint64_t half_exp_bits = ((uint64_t)half_exp) << 10;
    uint64_t half_man = man >> 42;
    uint64_t round_bit = ((uint64_t)1) << 41;
    uint64_t combined = half_sign_bits | half_exp_bits | half_man;
    bool round_up = (man & round_bit) != 0 && (man & (3 * round_bit - 1)) != 0;
    if (round_up) combined += 1;
    return (uint16_t)(combined & 0xffff);
}

double f16_bits_to_double(uint16_t bits) {
    uint64_t i = bits;
    uint64_t sign = (i >> 15) & 1;
    uint64_t exp = (i >> 10) & 0x1f;
    uint64_t man = i & 0x3ff;
    uint64_t sign64 = sign << 63;

    if (exp == 0 && man == 0) {
        return bits_double(sign64);
    }
    if (exp == 0) {
        /* Normalize the 10-bit subnormal significand by left-shifting until
         * its leading set bit reaches position 10 (the implicit-bit
         * position), counting shifts via exp_adj. exp_adj MUST start at 1,
         * not -1: for the smallest subnormal (man=1, true value 2^-24),
         * exactly 10 shifts are needed to reach manAdj=0x400, and the
         * final double exponent must come out to 1023-24=999. That only
         * holds if exp_adj lands on 1-10=-9 (giving 1008+(-9)=999); a
         * -1 starting point yields -11, undercounting by 2 and silently
         * halving (well, quartering: 2^-2) the reconstructed magnitude of
         * every subnormal f16 value. Verified against a from-scratch
         * derivation and an exhaustive f16-bit-pattern round-trip test
         * (tests/test_float16.c) covering all 2046 subnormal patterns. */
        int64_t exp_adj = 1;
        uint64_t man_adj = man;
        while ((man_adj & 0x400) == 0) {
            man_adj <<= 1;
            exp_adj -= 1;
        }
        man_adj &= 0x3ff;
        int64_t exp64 = 1023 - 15 + exp_adj;
        uint64_t man64 = man_adj << 42;
        return bits_double(sign64 | ((uint64_t)exp64 << 52) | man64);
    }
    if (exp == 0x1f) {
        if (man == 0) {
            return bits_double(sign64 | 0x7ff0000000000000ULL);
        }
        return bits_double(sign64 | 0x7ff0000000000000ULL | (man << 42));
    }
    uint64_t exp64 = exp - 15 + 1023;
    uint64_t man64 = man << 42;
    return bits_double(sign64 | (exp64 << 52) | man64);
}

bool same_bits(double a, double b) {
    return double_bits(a) == double_bits(b);
}

bool try_f16(double f, uint8_t out[2]) {
    uint16_t bits = double_to_f16_bits(f);
    double back = f16_bits_to_double(bits);
    if (!same_bits(back, f)) return false;
    out[0] = (uint8_t)(bits >> 8);
    out[1] = (uint8_t)(bits & 0xff);
    return true;
}

bool try_f32(double f, uint8_t out[4]) {
    float v = (float)f;
    double back = (double)v;
    if (!same_bits(back, f)) return false;
    uint32_t bits = float_bits(v);
    out[0] = (uint8_t)(bits >> 24);
    out[1] = (uint8_t)(bits >> 16);
    out[2] = (uint8_t)(bits >> 8);
    out[3] = (uint8_t)(bits & 0xff);
    return true;
}
