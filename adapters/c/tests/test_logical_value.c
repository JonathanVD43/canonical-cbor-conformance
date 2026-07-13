#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "logical_value.h"
#include "test_framework.h"

static LogicalValue *parse(const char *json) {
    char *err = NULL;
    LogicalValue *v = parse_logical_value_line(json, strlen(json), &err);
    free(err);
    return v;
}

void run_logical_value_tests(void) {
    LogicalValue *v = parse("{\"type\":\"int\",\"value\":\"42\"}");
    CHECK(v && v->type == LV_INT);
    CHECK(v && strcmp(v->as.intv.value, "42") == 0);
    logical_value_free(v);

    /* tag.tag is a JSON number up to 2^64-1 -- must not be narrowed through
     * a signed 64-bit intermediate (the upper half of the range is beyond
     * INT64_MAX). */
    v = parse("{\"type\":\"tag\",\"tag\":18446744073709551615,\"value\":{\"type\":\"null\"}}");
    CHECK(v && v->type == LV_TAG);
    CHECK(v && v->as.tagv.tag == UINT64_MAX);
    logical_value_free(v);

    v = parse("{\"type\":\"tag\",\"tag\":100,\"value\":{\"type\":\"int\",\"value\":\"5\"}}");
    CHECK(v && v->as.tagv.tag == 100);
    CHECK(v && v->as.tagv.value->type == LV_INT);
    logical_value_free(v);

    /* Negative tag numbers are invalid (schema minimum: 0). */
    v = parse("{\"type\":\"tag\",\"tag\":-1,\"value\":{\"type\":\"null\"}}");
    CHECK(v == NULL);

    /* tag overflowing 2^64-1 is invalid. */
    v = parse("{\"type\":\"tag\",\"tag\":18446744073709551616,\"value\":{\"type\":\"null\"}}");
    CHECK(v == NULL);

    v = parse("{\"type\":\"bignum\",\"sign\":\"negative\",\"value\":\"18446744073709551616\"}");
    CHECK(v && v->type == LV_BIGNUM);
    CHECK(v && strcmp(v->as.bignumv.sign, "negative") == 0);
    logical_value_free(v);

    v = parse("{\"type\":\"array\",\"items\":[{\"type\":\"int\",\"value\":\"1\"},{\"type\":\"bool\",\"value\":true}]}");
    CHECK(v && v->type == LV_ARRAY && v->as.arrayv.count == 2);
    logical_value_free(v);

    v = parse(
        "{\"type\":\"map\",\"entries\":[[{\"type\":\"text\",\"value\":\"a\"},{\"type\":\"int\",\"value\":\"1\"}]]}");
    CHECK(v && v->type == LV_MAP && v->as.mapv.count == 1);
    logical_value_free(v);

    /* Missing required fields / unknown type / malformed JSON are rejected. */
    CHECK(parse("{\"type\":\"int\"}") == NULL);
    CHECK(parse("{\"type\":\"bogus\"}") == NULL);
    CHECK(parse("not json") == NULL);
    CHECK(parse("{}") == NULL);
}
