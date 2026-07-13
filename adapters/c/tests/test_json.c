#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "test_framework.h"

static JsonValue *parse(const char *text, char **err) {
    return json_parse(text, strlen(text), err);
}

void run_json_tests(void) {
    char *err = NULL;

    JsonValue *v = parse("{\"a\":1,\"b\":\"hi\"}", &err);
    CHECK(v != NULL);
    if (v) {
        CHECK(v->type == JSON_OBJECT);
        JsonValue *a = json_object_get(v, "a");
        CHECK(a && a->type == JSON_NUMBER);
        CHECK(a && strcmp(a->as.number.digits, "1") == 0);
        JsonValue *b = json_object_get(v, "b");
        CHECK(b && b->type == JSON_STRING && b->as.string.len == 2);
        json_free(v);
    }

    /* Standard escapes. */
    v = parse("\"a\\nb\\tc\\\"d\\\\e\"", &err);
    CHECK(v != NULL);
    if (v) {
        CHECK(v->as.string.len == 9);
        CHECK(memcmp(v->as.string.data, "a\nb\tc\"d\\e", 9) == 0);
        json_free(v);
    }

    /* \u escape, including a literal NUL byte -- text values must be
     * length-explicit, not NUL-terminated-reliant. */
    v = parse("\"x\\u0000y\"", &err);
    CHECK(v != NULL);
    if (v) {
        CHECK(v->as.string.len == 3);
        CHECK(v->as.string.data[0] == 'x' && v->as.string.data[1] == '\0' && v->as.string.data[2] == 'y');
        json_free(v);
    }

    /* Surrogate pair \u escape for a codepoint outside the BMP (U+1F600). */
    v = parse("\"\\ud83d\\ude00\"", &err);
    CHECK(v != NULL);
    if (v) {
        CHECK(v->as.string.len == 4); /* U+1F600 is a 4-byte UTF-8 sequence */
        json_free(v);
    }

    /* Non-ASCII source bytes pass through untouched (café). */
    v = parse("\"caf\xc3\xa9\"", &err);
    CHECK(v != NULL);
    if (v) {
        CHECK(v->as.string.len == 5);
        json_free(v);
    }

    /* Arrays and nesting. */
    v = parse("[1,[2,3],{}]", &err);
    CHECK(v != NULL && v->type == JSON_ARRAY && v->as.array.count == 3);
    if (v && v->type == JSON_ARRAY && v->as.array.count == 3) {
        CHECK(v->as.array.items[1]->type == JSON_ARRAY && v->as.array.items[1]->as.array.count == 2);
    }
    json_free(v);

    /* Booleans and null. */
    v = parse("true", &err);
    CHECK(v && v->type == JSON_BOOL && v->as.boolean == true);
    json_free(v);
    v = parse("null", &err);
    CHECK(v && v->type == JSON_NULL);
    json_free(v);

    /* Large unsigned integer literal (tag field range) parses as a digit
     * string, not narrowed through a native int type. */
    v = parse("18446744073709551615", &err);
    CHECK(v && v->type == JSON_NUMBER);
    if (v && v->type == JSON_NUMBER) {
        CHECK(strcmp(v->as.number.digits, "18446744073709551615") == 0);
    }
    json_free(v);

    /* Malformed input is rejected, not silently mis-parsed. */
    v = parse("{\"a\":}", &err);
    CHECK(v == NULL);
    free(err);
    err = NULL;

    v = parse("[1,2", &err);
    CHECK(v == NULL);
    free(err);
    err = NULL;

    v = parse("\"unterminated", &err);
    CHECK(v == NULL);
    free(err);
    err = NULL;

    v = parse("123 456", &err);
    CHECK(v == NULL); /* trailing characters after the value */
    free(err);
}
