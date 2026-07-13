#ifndef ADAPTER_LOGICAL_VALUE_H
#define ADAPTER_LOGICAL_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* In-memory model of a single "logical value" (logical-value.schema.json),
 * parsed from one stdin JSON line. Mirrors adapters/go/logicalvalue.go's
 * LogicalValue interface / concrete struct set, and (like every other
 * adapter) keeps int/float/bignum values as decimal-string text rather
 * than parsing them through a native numeric type up front -- magnitudes
 * can exceed what a native 64-bit type holds, and how far out of range a
 * value is (vs. simply "invalid") matters for producing the right
 * accept/reject behavior downstream. */

typedef enum {
    LV_INT,
    LV_FLOAT,
    LV_TEXT,
    LV_BYTES,
    LV_BOOL,
    LV_NULL,
    LV_ARRAY,
    LV_MAP,
    LV_TAG,
    LV_BIGNUM
} LVType;

typedef struct LogicalValue LogicalValue;

typedef struct {
    LogicalValue *key;
    LogicalValue *val;
} LVMapEntry;

struct LogicalValue {
    LVType type;
    union {
        struct {
            char *value; /* decimal string, NUL-terminated, owned */
        } intv;
        struct {
            char *width; /* "auto" | "f16" | "f32" | "f64", owned */
            char *value;
        } floatv;
        struct {
            uint8_t *bytes; /* UTF-8, owned; NOT NUL-terminated-reliant */
            size_t len;
        } textv;
        struct {
            uint8_t *bytes; /* owned; ASCII hex digit text (not yet decoded --
                                decoded at encode time, mirroring the Go/Rust
                                adapters' BytesValue{Value: hexString}) */
            size_t len;      /* length of the hex text, i.e. 2x the decoded byte count */
        } bytesv;
        bool boolv;
        struct {
            LogicalValue **items;
            size_t count;
        } arrayv;
        struct {
            LVMapEntry *entries;
            size_t count;
        } mapv;
        struct {
            uint64_t tag;
            LogicalValue *value;
        } tagv;
        struct {
            char *sign;  /* "positive" | "negative", owned */
            char *value; /* decimal magnitude string, owned */
        } bignumv;
    } as;
};

void logical_value_free(LogicalValue *v);

/* Parses a single stdin line (already-trimmed, NUL-terminated JSON object
 * text) into a LogicalValue tree. On success returns the tree and sets
 * *err to NULL; on failure returns NULL and sets *err to a newly malloc'd
 * (caller-freed) diagnostic string. */
LogicalValue *parse_logical_value_line(const char *line, size_t len, char **err);

#endif
