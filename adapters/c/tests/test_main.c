#include <stdio.h>

#include "test_framework.h"

int g_test_total = 0;
int g_test_fail = 0;

void run_float16_tests(void);
void run_bignum128_tests(void);
void run_cbor_common_tests(void);
void run_rfc8949_tests(void);
void run_dcbor_tests(void);
void run_json_tests(void);
void run_logical_value_tests(void);
void run_decode_tests(void);
void run_nfc_tests(void);

int main(void) {
    run_float16_tests();
    run_bignum128_tests();
    run_cbor_common_tests();
    run_rfc8949_tests();
    run_dcbor_tests();
    run_json_tests();
    run_logical_value_tests();
    run_decode_tests();
    run_nfc_tests();

    printf("%d/%d checks passed\n", g_test_total - g_test_fail, g_test_total);
    if (g_test_fail > 0) {
        printf("%d FAILURES\n", g_test_fail);
        return 1;
    }
    return 0;
}
