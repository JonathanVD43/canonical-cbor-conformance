#include "util.h"

#include <stdlib.h>
#include <string.h>

static size_t grow_cap(size_t cap, size_t need) {
    if (cap == 0) cap = 16;
    while (cap < need) cap *= 2;
    return cap;
}

void bytebuf_init(ByteBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void bytebuf_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void bytebuf_reserve(ByteBuf *b, size_t extra) {
    size_t need = b->len + extra;
    if (need <= b->cap) return;
    size_t newcap = grow_cap(b->cap, need);
    uint8_t *n = realloc(b->data, newcap);
    if (!n) {
        /* Conformance adapter, not a library: an allocation failure here is
         * unrecoverable and there is no sane fallback path. */
        abort();
    }
    b->data = n;
    b->cap = newcap;
}

void bytebuf_push(ByteBuf *b, uint8_t byte) {
    bytebuf_reserve(b, 1);
    b->data[b->len++] = byte;
}

void bytebuf_append(ByteBuf *b, const uint8_t *data, size_t len) {
    if (len == 0) return;
    bytebuf_reserve(b, len);
    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void bytebuf_append_be(ByteBuf *b, uint64_t v, size_t width) {
    bytebuf_reserve(b, width);
    for (size_t i = 0; i < width; i++) {
        size_t shift = (width - 1 - i) * 8;
        b->data[b->len + i] = (uint8_t)((v >> shift) & 0xff);
    }
    b->len += width;
}

void strbuf_init(StrBuf *s) {
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

void strbuf_free(StrBuf *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static void strbuf_reserve(StrBuf *s, size_t extra) {
    size_t need = s->len + extra + 1; /* +1 for NUL */
    if (need <= s->cap) return;
    size_t newcap = grow_cap(s->cap, need);
    char *n = realloc(s->data, newcap);
    if (!n) abort();
    s->data = n;
    s->cap = newcap;
}

void strbuf_append_char(StrBuf *s, char c) {
    strbuf_reserve(s, 1);
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

void strbuf_append_str(StrBuf *s, const char *str) {
    size_t l = strlen(str);
    strbuf_reserve(s, l);
    memcpy(s->data + s->len, str, l);
    s->len += l;
    s->data[s->len] = '\0';
}

static const char HEX_DIGITS[] = "0123456789abcdef";

char *hex_encode(const uint8_t *data, size_t len) {
    char *out = malloc(len * 2 + 1);
    if (!out) abort();
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = HEX_DIGITS[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = HEX_DIGITS[data[i] & 0xf];
    }
    out[len * 2] = '\0';
    return out;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(const char *str, size_t str_len, uint8_t **out_data, size_t *out_len) {
    *out_data = NULL;
    *out_len = 0;
    if (str_len % 2 != 0) return false;
    size_t n = str_len / 2;
    uint8_t *out = malloc(n > 0 ? n : 1);
    if (!out) abort();
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(str[i * 2]);
        int lo = hex_val(str[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_data = out;
    *out_len = n;
    return true;
}

int compare_bytes_unsigned(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
    size_t min_len = a_len < b_len ? a_len : b_len;
    if (min_len > 0) {
        int c = memcmp(a, b, min_len);
        if (c != 0) return c < 0 ? -1 : 1;
    }
    if (a_len == b_len) return 0;
    return a_len < b_len ? -1 : 1;
}

static void stable_merge(size_t *order, size_t *tmp, size_t lo, size_t mid, size_t hi, IndexCompareFn cmp, void *ctx) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (cmp(order[i], order[j], ctx) <= 0) {
            tmp[k++] = order[i++];
        } else {
            tmp[k++] = order[j++];
        }
    }
    while (i < mid) tmp[k++] = order[i++];
    while (j < hi) tmp[k++] = order[j++];
    memcpy(order + lo, tmp + lo, (hi - lo) * sizeof(size_t));
}

static void stable_msort(size_t *order, size_t *tmp, size_t lo, size_t hi, IndexCompareFn cmp, void *ctx) {
    if (hi - lo <= 1) return;
    size_t mid = lo + (hi - lo) / 2;
    stable_msort(order, tmp, lo, mid, cmp, ctx);
    stable_msort(order, tmp, mid, hi, cmp, ctx);
    stable_merge(order, tmp, lo, mid, hi, cmp, ctx);
}

void stable_sort_indices(size_t n, size_t *order, IndexCompareFn cmp, void *ctx) {
    for (size_t i = 0; i < n; i++) order[i] = i;
    if (n < 2) return;
    size_t *tmp = malloc(n * sizeof(size_t));
    if (!tmp) abort();
    stable_msort(order, tmp, 0, n, cmp, ctx);
    free(tmp);
}
