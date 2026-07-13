#include "json.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
} Parser;

static char *dup_err(const char *msg) {
    size_t l = strlen(msg);
    char *out = malloc(l + 1);
    if (!out) abort();
    memcpy(out, msg, l + 1);
    return out;
}

static void skip_ws(Parser *p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static int peek(Parser *p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->s[p->pos];
}

static JsonValue *alloc_value(JsonType t) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    if (!v) abort();
    v->type = t;
    return v;
}

static JsonValue *parse_value(Parser *p, char **err);

static bool literal(Parser *p, const char *lit) {
    size_t l = strlen(lit);
    if (p->pos + l > p->len) return false;
    if (memcmp(p->s + p->pos, lit, l) != 0) return false;
    p->pos += l;
    return true;
}

/* Appends the UTF-8 encoding of a codepoint to a growable buffer. */
static void push_utf8(uint8_t **buf, size_t *len, size_t *cap, uint32_t cp) {
    uint8_t tmp[4];
    size_t n;
    if (cp < 0x80) {
        tmp[0] = (uint8_t)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (uint8_t)(0xc0 | (cp >> 6));
        tmp[1] = (uint8_t)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (uint8_t)(0xe0 | (cp >> 12));
        tmp[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        tmp[2] = (uint8_t)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        tmp[0] = (uint8_t)(0xf0 | (cp >> 18));
        tmp[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
        tmp[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        tmp[3] = (uint8_t)(0x80 | (cp & 0x3f));
        n = 4;
    }
    if (*len + n > *cap) {
        size_t newcap = *cap == 0 ? 16 : *cap * 2;
        while (newcap < *len + n) newcap *= 2;
        uint8_t *nb = realloc(*buf, newcap);
        if (!nb) abort();
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, tmp, n);
    *len += n;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex4(Parser *p, uint32_t *out) {
    if (p->pos + 4 > p->len) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex_digit(p->s[p->pos + i]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    p->pos += 4;
    *out = v;
    return true;
}

static JsonValue *parse_string_value(Parser *p, char **err) {
    /* Assumes p->pos currently points at the opening quote. */
    p->pos++; /* consume opening quote */
    uint8_t *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (p->pos >= p->len) {
            free(buf);
            *err = dup_err("unterminated string literal");
            return NULL;
        }
        unsigned char c = (unsigned char)p->s[p->pos];
        if (c == '"') {
            p->pos++;
            break;
        }
        if (c == '\\') {
            p->pos++;
            if (p->pos >= p->len) {
                free(buf);
                *err = dup_err("unterminated escape sequence");
                return NULL;
            }
            char esc = p->s[p->pos];
            p->pos++;
            switch (esc) {
                case '"': push_utf8(&buf, &len, &cap, '"'); break;
                case '\\': push_utf8(&buf, &len, &cap, '\\'); break;
                case '/': push_utf8(&buf, &len, &cap, '/'); break;
                case 'b': push_utf8(&buf, &len, &cap, 0x08); break;
                case 'f': push_utf8(&buf, &len, &cap, 0x0c); break;
                case 'n': push_utf8(&buf, &len, &cap, '\n'); break;
                case 'r': push_utf8(&buf, &len, &cap, '\r'); break;
                case 't': push_utf8(&buf, &len, &cap, '\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!parse_hex4(p, &cp)) {
                        free(buf);
                        *err = dup_err("invalid \\u escape");
                        return NULL;
                    }
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        /* High surrogate: require a following \uXXXX low surrogate. */
                        if (p->pos + 1 < p->len && p->s[p->pos] == '\\' && p->s[p->pos + 1] == 'u') {
                            Parser save = *p;
                            p->pos += 2;
                            uint32_t lo;
                            if (parse_hex4(p, &lo) && lo >= 0xdc00 && lo <= 0xdfff) {
                                uint32_t combined = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                                push_utf8(&buf, &len, &cap, combined);
                                break;
                            }
                            *p = save;
                        }
                        /* Unpaired surrogate: emit replacement character
                         * rather than producing invalid UTF-8. */
                        push_utf8(&buf, &len, &cap, 0xfffd);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        push_utf8(&buf, &len, &cap, 0xfffd);
                    } else {
                        push_utf8(&buf, &len, &cap, cp);
                    }
                    break;
                }
                default:
                    free(buf);
                    *err = dup_err("invalid escape character");
                    return NULL;
            }
            continue;
        }
        if (c < 0x20) {
            free(buf);
            *err = dup_err("unescaped control character in string");
            return NULL;
        }
        /* Raw byte (part of the source's already-UTF-8-encoded text or a
         * continuation byte); copy through verbatim. */
        if (len + 1 > cap) {
            size_t newcap = cap == 0 ? 16 : cap * 2;
            uint8_t *nb = realloc(buf, newcap);
            if (!nb) abort();
            buf = nb;
            cap = newcap;
        }
        buf[len++] = c;
        p->pos++;
    }
    /* NUL-terminate for callers that treat known-ASCII string values (field
     * names, "width", "sign", decimal literals) as plain C strings; `len`
     * remains the authoritative byte length for values that may embed a
     * real NUL (arbitrary text content via \x00). */
    uint8_t *nulterm = realloc(buf, len + 1);
    if (!nulterm) abort();
    nulterm[len] = '\0';
    JsonValue *v = alloc_value(JSON_STRING);
    v->as.string.data = nulterm;
    v->as.string.len = len;
    return v;
}

static JsonValue *parse_number_value(Parser *p, char **err) {
    size_t start = p->pos;
    if (peek(p) == '-') p->pos++;
    if (p->pos >= p->len || p->s[p->pos] < '0' || p->s[p->pos] > '9') {
        p->pos = start;
        *err = dup_err("invalid number literal");
        return NULL;
    }
    while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') p->pos++;
    /* This project's grammar never uses a fractional/exponent JSON number
     * (only the non-negative integer "tag" field is a JSON number) -- reject
     * anything beyond a plain (optionally signed) integer literal rather
     * than silently mis-parsing it. */
    if (p->pos < p->len && (p->s[p->pos] == '.' || p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        *err = dup_err("fractional/exponent JSON numbers are not supported by this adapter's grammar");
        return NULL;
    }
    size_t len = p->pos - start;
    JsonValue *v = alloc_value(JSON_NUMBER);
    char *digits = malloc(len + 1);
    if (!digits) abort();
    memcpy(digits, p->s + start, len);
    digits[len] = '\0';
    v->as.number.digits = digits;
    v->as.number.len = len;
    return v;
}

static JsonValue *parse_array_value(Parser *p, char **err) {
    p->pos++; /* consume '[' */
    JsonValue *v = alloc_value(JSON_ARRAY);
    JsonValue **items = NULL;
    size_t count = 0, cap = 0;
    skip_ws(p);
    if (peek(p) == ']') {
        p->pos++;
        v->as.array.items = items;
        v->as.array.count = count;
        return v;
    }
    for (;;) {
        skip_ws(p);
        JsonValue *item = parse_value(p, err);
        if (!item) {
            for (size_t i = 0; i < count; i++) json_free(items[i]);
            free(items);
            free(v);
            return NULL;
        }
        if (count == cap) {
            cap = cap == 0 ? 8 : cap * 2;
            JsonValue **n = realloc(items, cap * sizeof(JsonValue *));
            if (!n) abort();
            items = n;
        }
        items[count++] = item;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == ']') {
            p->pos++;
            break;
        }
        for (size_t i = 0; i < count; i++) json_free(items[i]);
        free(items);
        free(v);
        *err = dup_err("expected ',' or ']' in array");
        return NULL;
    }
    v->as.array.items = items;
    v->as.array.count = count;
    return v;
}

static JsonValue *parse_object_value(Parser *p, char **err) {
    p->pos++; /* consume '{' */
    JsonValue *v = alloc_value(JSON_OBJECT);
    JsonMember *members = NULL;
    size_t count = 0, cap = 0;
    skip_ws(p);
    if (peek(p) == '}') {
        p->pos++;
        v->as.object.members = members;
        v->as.object.count = count;
        return v;
    }
    for (;;) {
        skip_ws(p);
        if (peek(p) != '"') {
            *err = dup_err("expected string object key");
            goto fail;
        }
        JsonValue *key_val = parse_string_value(p, err);
        if (!key_val) goto fail;
        skip_ws(p);
        if (peek(p) != ':') {
            json_free(key_val);
            *err = dup_err("expected ':' after object key");
            goto fail;
        }
        p->pos++;
        skip_ws(p);
        JsonValue *val = parse_value(p, err);
        if (!val) {
            json_free(key_val);
            goto fail;
        }
        if (count == cap) {
            cap = cap == 0 ? 8 : cap * 2;
            JsonMember *n = realloc(members, cap * sizeof(JsonMember));
            if (!n) abort();
            members = n;
        }
        /* Object keys are always short ASCII field names in this grammar;
         * NUL-terminated char* is fine here (unlike text logical values). */
        char *key = malloc(key_val->as.string.len + 1);
        if (!key) abort();
        memcpy(key, key_val->as.string.data, key_val->as.string.len);
        key[key_val->as.string.len] = '\0';
        json_free(key_val);
        members[count].key = key;
        members[count].value = val;
        count++;
        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == '}') {
            p->pos++;
            break;
        }
        *err = dup_err("expected ',' or '}' in object");
        goto fail;
    }
    v->as.object.members = members;
    v->as.object.count = count;
    return v;

fail:
    for (size_t i = 0; i < count; i++) {
        free(members[i].key);
        json_free(members[i].value);
    }
    free(members);
    free(v);
    return NULL;
}

static JsonValue *parse_value(Parser *p, char **err) {
    skip_ws(p);
    int c = peek(p);
    if (c < 0) {
        *err = dup_err("unexpected end of input");
        return NULL;
    }
    if (c == '"') return parse_string_value(p, err);
    if (c == '{') return parse_object_value(p, err);
    if (c == '[') return parse_array_value(p, err);
    if (c == 't') {
        if (literal(p, "true")) {
            JsonValue *v = alloc_value(JSON_BOOL);
            v->as.boolean = true;
            return v;
        }
        *err = dup_err("invalid literal");
        return NULL;
    }
    if (c == 'f') {
        if (literal(p, "false")) {
            JsonValue *v = alloc_value(JSON_BOOL);
            v->as.boolean = false;
            return v;
        }
        *err = dup_err("invalid literal");
        return NULL;
    }
    if (c == 'n') {
        if (literal(p, "null")) {
            return alloc_value(JSON_NULL);
        }
        *err = dup_err("invalid literal");
        return NULL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number_value(p, err);
    }
    *err = dup_err("unexpected character");
    return NULL;
}

JsonValue *json_parse(const char *text, size_t len, char **err) {
    Parser p = {text, len, 0};
    *err = NULL;
    JsonValue *v = parse_value(&p, err);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != p.len) {
        json_free(v);
        *err = dup_err("trailing characters after JSON value");
        return NULL;
    }
    return v;
}

void json_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->as.string.data);
            break;
        case JSON_NUMBER:
            free(v->as.number.digits);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) json_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.object.count; i++) {
                free(v->as.object.members[i].key);
                json_free(v->as.object.members[i].value);
            }
            free(v->as.object.members);
            break;
        default:
            break;
    }
    free(v);
}

JsonValue *json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.members[i].key, key) == 0) {
            return obj->as.object.members[i].value;
        }
    }
    return NULL;
}
