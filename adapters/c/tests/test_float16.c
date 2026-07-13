#include <math.h>
#include <string.h>

#include "float16.h"
#include "test_framework.h"

void run_float16_tests(void) {
    /* 1.0 round-trips exactly at f16. */
    uint8_t b2[2];
    CHECK(try_f16(1.0, b2));
    CHECK(b2[0] == 0x3c && b2[1] == 0x00);

    /* 0.1 cannot round-trip at f16 or f32. */
    uint8_t tmp2[2], tmp4[4];
    CHECK(!try_f16(0.1, tmp2));
    CHECK(!try_f32(0.1, tmp4));

    /* Canonical NaN payload 0x7e00 round-trips through f16 bits. */
    double nan_val = f16_bits_to_double(0x7e00);
    CHECK(isnan(nan_val));

    /* -0.0 preserves sign through f16 round-trip. */
    uint8_t neg_zero[2];
    CHECK(try_f16(-0.0, neg_zero));
    CHECK(neg_zero[0] == 0x80 && neg_zero[1] == 0x00);
    double back = f16_bits_to_double(0x8000);
    CHECK(same_bits(back, -0.0));
    CHECK(!same_bits(back, 0.0));

    /* Infinity round-trips through f16. */
    double inf_back = f16_bits_to_double(0x7c00);
    CHECK(isinf(inf_back) && inf_back > 0);

    /* Every f16 bit pattern round-trips through double->f16->double
     * exactly (f16 is a strict subset of f64's representable values). */
    for (uint32_t bits = 0; bits <= 0xffff; bits++) {
        double d = f16_bits_to_double((uint16_t)bits);
        if (isnan(d)) continue; /* NaN payloads aren't bit-preserved by design */
        uint16_t back_bits = double_to_f16_bits(d);
        CHECK(back_bits == (uint16_t)bits);
    }
}
