#include "logical_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bignum128.h"
#include "json.h"

static char *dup_err(const char *msg) {
    size_t l = strlen(msg);
    char *out = malloc(l + 1);
    if (!out) abort();
    memcpy(out, msg, l + 1);
    return out;
}

static char *dup_cstr(const uint8_t *data, size_t len) {
    char *out = malloc(len + 1);
    if (!out) abort();
    memcpy(out, data, len);
    out[len] = '\0';
    return out;
}

void logical_value_free(LogicalValue *v) {
    if (!v) return;
    switch (v->type) {
        case LV_INT:
            free(v->as.intv.value);
            break;
        case LV_FLOAT:
            free(v->as.floatv.width);
            free(v->as.floatv.value);
            break;
        case LV_TEXT:
            free(v->as.textv.bytes);
            break;
        case LV_BYTES:
            free(v->as.bytesv.bytes);
            break;
        case LV_ARRAY:
            for (size_t i = 0; i < v->as.arrayv.count; i++) logical_value_free(v->as.arrayv.items[i]);
            free(v->as.arrayv.items);
            break;
        case LV_MAP:
            for (size_t i = 0; i < v->as.mapv.count; i++) {
                logical_value_free(v->as.mapv.entries[i].key);
                logical_value_free(v->as.mapv.entries[i].val);
            }
            free(v->as.mapv.entries);
            break;
        case LV_TAG:
            logical_value_free(v->as.tagv.value);
            break;
        case LV_BIGNUM:
            free(v->as.bignumv.sign);
            free(v->as.bignumv.value);
            break;
        case LV_BOOL:
        case LV_NULL:
        default:
            break;
    }
    free(v);
}

static LogicalValue *alloc_lv(LVType t) {
    LogicalValue *v = calloc(1, sizeof(LogicalValue));
    if (!v) abort();
    v->type = t;
    return v;
}

/* Extracts a required string-typed field as a NUL-terminated C string.
 * Returns NULL + sets *err on a missing/wrong-typed field. */
static char *required_string(const JsonValue *obj, const char *field, const char *type_name, char **err) {
    JsonValue *v = json_object_get(obj, field);
    if (!v || v->type != JSON_STRING) {
        char buf[128];
        snprintf(buf, sizeof buf, "%s: missing \"%s\"", type_name, field);
        *err = dup_err(buf);
        return NULL;
    }
    return dup_cstr(v->as.string.data, v->as.string.len);
}

static LogicalValue *parse_lv(const JsonValue *raw, char **err);

static LogicalValue *parse_array_field(const JsonValue *obj, char **err) {
    JsonValue *items = json_object_get(obj, "items");
    if (!items || items->type != JSON_ARRAY) {
        *err = dup_err("array: missing \"items\"");
        return NULL;
    }
    LogicalValue *v = alloc_lv(LV_ARRAY);
    size_t n = items->as.array.count;
    LogicalValue **out = n > 0 ? malloc(n * sizeof(LogicalValue *)) : NULL;
    if (n > 0 && !out) abort();
    for (size_t i = 0; i < n; i++) {
        LogicalValue *item = parse_lv(items->as.array.items[i], err);
        if (!item) {
            for (size_t j = 0; j < i; j++) logical_value_free(out[j]);
            free(out);
            free(v);
            return NULL;
        }
        out[i] = item;
    }
    v->as.arrayv.items = out;
    v->as.arrayv.count = n;
    return v;
}

static LogicalValue *parse_map_field(const JsonValue *obj, char **err) {
    JsonValue *entries = json_object_get(obj, "entries");
    if (!entries || entries->type != JSON_ARRAY) {
        *err = dup_err("map: missing \"entries\"");
        return NULL;
    }
    LogicalValue *v = alloc_lv(LV_MAP);
    size_t n = entries->as.array.count;
    LVMapEntry *out = n > 0 ? malloc(n * sizeof(LVMapEntry)) : NULL;
    if (n > 0 && !out) abort();
    size_t built = 0;
    for (size_t i = 0; i < n; i++) {
        JsonValue *pair = entries->as.array.items[i];
        if (!pair || pair->type != JSON_ARRAY || pair->as.array.count != 2) {
            *err = dup_err("map entry must be a 2-element array");
            goto fail;
        }
        LogicalValue *k = parse_lv(pair->as.array.items[0], err);
        if (!k) goto fail;
        LogicalValue *val = parse_lv(pair->as.array.items[1], err);
        if (!val) {
            logical_value_free(k);
            goto fail;
        }
        out[built].key = k;
        out[built].val = val;
        built++;
    }
    v->as.mapv.entries = out;
    v->as.mapv.count = n;
    return v;

fail:
    for (size_t j = 0; j < built; j++) {
        logical_value_free(out[j].key);
        logical_value_free(out[j].val);
    }
    free(out);
    free(v);
    return NULL;
}

static LogicalValue *parse_tag_field(const JsonValue *obj, char **err) {
    JsonValue *tag_json = json_object_get(obj, "tag");
    if (!tag_json || tag_json->type != JSON_NUMBER) {
        *err = dup_err("tag: missing \"tag\" number");
        return NULL;
    }
    /* tag.tag is a JSON number in [0, 2^64-1] (logical-value.schema.json) --
     * this exceeds a signed 64-bit type's range in the upper half, so it's
     * parsed via the same overflow-checked u128 decimal parser used for
     * bignum magnitudes, not narrowed through a native int/long parse. */
    if (tag_json->as.number.len > 0 && tag_json->as.number.digits[0] == '-') {
        *err = dup_err("tag: missing \"tag\" number");
        return NULL;
    }
    u128 val;
    if (!u128_parse_decimal(tag_json->as.number.digits, tag_json->as.number.len, &val)) {
        *err = dup_err("tag: missing \"tag\" number");
        return NULL;
    }
    if (val > (u128)UINT64_MAX) {
        *err = dup_err("tag: missing \"tag\" number");
        return NULL;
    }
    JsonValue *inner_json = json_object_get(obj, "value");
    if (!inner_json) {
        *err = dup_err("tag: missing \"value\"");
        return NULL;
    }
    LogicalValue *inner = parse_lv(inner_json, err);
    if (!inner) return NULL;
    LogicalValue *v = alloc_lv(LV_TAG);
    v->as.tagv.tag = (uint64_t)val;
    v->as.tagv.value = inner;
    return v;
}

static LogicalValue *parse_lv(const JsonValue *raw, char **err) {
    if (!raw || raw->type != JSON_OBJECT) {
        *err = dup_err("expected a JSON object");
        return NULL;
    }
    JsonValue *type_json = json_object_get(raw, "type");
    if (!type_json || type_json->type != JSON_STRING) {
        *err = dup_err("missing \"type\" field");
        return NULL;
    }
    const char *t = (const char *)type_json->as.string.data;
    size_t tlen = type_json->as.string.len;

#define TYPE_IS(lit) (tlen == strlen(lit) && memcmp(t, lit, tlen) == 0)

    if (TYPE_IS("int")) {
        char *value = required_string(raw, "value", "int", err);
        if (!value) return NULL;
        LogicalValue *v = alloc_lv(LV_INT);
        v->as.intv.value = value;
        return v;
    }
    if (TYPE_IS("float")) {
        char *width = required_string(raw, "width", "float", err);
        if (!width) return NULL;
        char *value = required_string(raw, "value", "float", err);
        if (!value) {
            free(width);
            return NULL;
        }
        LogicalValue *v = alloc_lv(LV_FLOAT);
        v->as.floatv.width = width;
        v->as.floatv.value = value;
        return v;
    }
    if (TYPE_IS("text")) {
        JsonValue *val = json_object_get(raw, "value");
        if (!val || val->type != JSON_STRING) {
            *err = dup_err("text: missing \"value\"");
            return NULL;
        }
        LogicalValue *v = alloc_lv(LV_TEXT);
        uint8_t *bytes = val->as.string.len > 0 ? malloc(val->as.string.len) : malloc(1);
        if (!bytes) abort();
        memcpy(bytes, val->as.string.data, val->as.string.len);
        v->as.textv.bytes = bytes;
        v->as.textv.len = val->as.string.len;
        return v;
    }
    if (TYPE_IS("bytes")) {
        char *value = required_string(raw, "value", "bytes", err);
        if (!value) return NULL;
        LogicalValue *v = alloc_lv(LV_BYTES);
        v->as.bytesv.bytes = (uint8_t *)value; /* hex string; decoded at encode time */
        v->as.bytesv.len = strlen(value);
        return v;
    }
    if (TYPE_IS("bool")) {
        JsonValue *val = json_object_get(raw, "value");
        if (!val || val->type != JSON_BOOL) {
            *err = dup_err("bool: missing \"value\"");
            return NULL;
        }
        LogicalValue *v = alloc_lv(LV_BOOL);
        v->as.boolv = val->as.boolean;
        return v;
    }
    if (TYPE_IS("null")) {
        return alloc_lv(LV_NULL);
    }
    if (TYPE_IS("array")) return parse_array_field(raw, err);
    if (TYPE_IS("map")) return parse_map_field(raw, err);
    if (TYPE_IS("tag")) return parse_tag_field(raw, err);
    if (TYPE_IS("bignum")) {
        char *sign = required_string(raw, "sign", "bignum", err);
        if (!sign) return NULL;
        char *value = required_string(raw, "value", "bignum", err);
        if (!value) {
            free(sign);
            return NULL;
        }
        LogicalValue *v = alloc_lv(LV_BIGNUM);
        v->as.bignumv.sign = sign;
        v->as.bignumv.value = value;
        return v;
    }
#undef TYPE_IS

    char buf[256];
    snprintf(buf, sizeof buf, "unknown logical-value type: \"%.*s\"", (int)tlen, t);
    *err = dup_err(buf);
    return NULL;
}

LogicalValue *parse_logical_value_line(const char *line, size_t len, char **err) {
    JsonValue *raw = json_parse(line, len, err);
    if (!raw) return NULL;
    LogicalValue *v = parse_lv(raw, err);
    json_free(raw);
    return v;
}
