#ifndef ADAPTER_JSON_H
#define ADAPTER_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal hand-rolled JSON parser -- C has no stdlib JSON support, and per
 * CONTRIBUTING.md's stdlib/hand-rolled-first convention a third-party JSON
 * library was ruled out. Scoped to exactly what this project's Neutral
 * Logical Value Grammar (logical-value.schema.json) needs: objects,
 * arrays, strings (with standard JSON escapes, including backslash-u
 * escapes and surrogate pairs), booleans, null, and non-negative integer-
 * literal numbers (only ever used for the "tag" field, which the schema
 * bounds to [0, 2^64-1] -- no fractional/exponent number support is needed
 * and none is implemented). Every string value is byte-length-explicit
 * (not NUL-terminated-reliant) since a backslash-u escape can embed a real
 * NUL byte in adapter text values. */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *key; /* NUL-terminated; object keys are always ASCII field names in this grammar */
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        struct {
            char *digits; /* NUL-terminated decimal digit string, no sign (schema: tag >= 0) */
            size_t len;
        } number;
        struct {
            uint8_t *data; /* UTF-8 encoded, NUL-terminated for convenience but len is authoritative */
            size_t len;
        } string;
        struct {
            JsonValue **items;
            size_t count;
        } array;
        struct {
            JsonMember *members;
            size_t count;
        } object;
    } as;
};

/* Parses a single JSON value from a NUL-terminated buffer. On success
 * returns a newly allocated JsonValue tree (free with json_free) and sets
 * *err to NULL. On failure returns NULL and sets *err to a newly malloc'd
 * (caller-freed) diagnostic string. Trailing whitespace after the value is
 * tolerated; trailing non-whitespace is an error (this project's stdin
 * protocol is one JSON value per line). */
JsonValue *json_parse(const char *text, size_t len, char **err);

void json_free(JsonValue *v);

/* Accessors returning NULL/false on type mismatch or missing key. */
JsonValue *json_object_get(const JsonValue *obj, const char *key);

#endif
