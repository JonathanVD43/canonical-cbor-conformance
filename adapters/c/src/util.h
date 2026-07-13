#ifndef ADAPTER_UTIL_H
#define ADAPTER_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Growable byte buffer used throughout the encoder/decoder. Owns its
 * storage; free with bytebuf_free. */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} ByteBuf;

void bytebuf_init(ByteBuf *b);
void bytebuf_free(ByteBuf *b);
void bytebuf_reserve(ByteBuf *b, size_t extra);
void bytebuf_push(ByteBuf *b, uint8_t byte);
void bytebuf_append(ByteBuf *b, const uint8_t *data, size_t len);
/* Appends the low `width` bytes of v, big-endian (width in {1,2,4,8}). */
void bytebuf_append_be(ByteBuf *b, uint64_t v, size_t width);

/* Growable, NUL-terminated string buffer (used for building the JSON
 * literal-text / diagnostics). Text logical values carry an explicit length
 * separately (ByteBuf) since they may embed NUL bytes; this type is only
 * used where we know the content is a plain diagnostic or decimal literal. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void strbuf_init(StrBuf *s);
void strbuf_free(StrBuf *s);
void strbuf_append_char(StrBuf *s, char c);
void strbuf_append_str(StrBuf *s, const char *str);

/* Lowercase hex encode of `len` bytes into a newly malloc'd NUL-terminated
 * string (caller frees). */
char *hex_encode(const uint8_t *data, size_t len);

/* Decodes a lowercase-or-uppercase hex string (even length required) into a
 * newly malloc'd byte buffer (caller frees via free()). Returns false on
 * malformed hex (odd length or non-hex-digit), in which case *out_len is
 * unspecified and no allocation is returned (out_data set to NULL). */
bool hex_decode(const char *str, size_t str_len, uint8_t **out_data, size_t *out_len);

/* Bytewise-lexicographic comparison, shorter-is-less on a shared prefix
 * (memcmp semantics extended with a length tiebreak) -- P2/D3's map-key
 * sort rule, and also used by decode-strict's UNSORTED_MAP_KEYS check. */
int compare_bytes_unsigned(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len);

/* Generic stable sort over an index permutation, used for map-key sorting
 * (P2/D3). C's stdlib qsort() is explicitly *not* guaranteed stable, and
 * map-key sort order must be deterministic and encounter-order-preserving
 * for equal keys (relevant to rfc8949, which allows literal duplicate keys
 * through unmodified rather than de-duping them) -- so this hand-rolls a
 * straightforward top-down merge sort (stable by construction) instead of
 * relying on qsort's unspecified behavior on ties.
 *
 * `order` must be a caller-allocated array of length n; on return,
 * order[i] holds the index (into the caller's own data) of the ith
 * smallest element per `cmp`, ties broken by original order. */
typedef int (*IndexCompareFn)(size_t a, size_t b, void *ctx);
void stable_sort_indices(size_t n, size_t *order, IndexCompareFn cmp, void *ctx);

#endif
