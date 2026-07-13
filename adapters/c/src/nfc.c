#include "nfc.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "nfc_tables.h"

enum {
    HANGUL_SBASE = 0xAC00,
    HANGUL_LBASE = 0x1100,
    HANGUL_VBASE = 0x1161,
    HANGUL_TBASE = 0x11A7,
    HANGUL_LCOUNT = 19,
    HANGUL_VCOUNT = 21,
    HANGUL_TCOUNT = 28,
};
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)

static bool is_hangul_syllable(uint32_t cp) {
    return cp >= HANGUL_SBASE && cp < HANGUL_SBASE + HANGUL_SCOUNT;
}

/* --- UTF-8 <-> codepoint conversion (input is always already-valid UTF-8: --
 * this project's decode-strict path rejects invalid UTF-8 before it ever
 * reaches here, and the JSON parser only ever produces valid UTF-8). A
 * malformed byte here is handled defensively by treating it as a single
 * U+FFFD-width skip rather than reading out of bounds. */

typedef struct {
    uint32_t *data;
    size_t len;
    size_t cap;
} CpBuf;

static void cpbuf_push(CpBuf *b, uint32_t cp) {
    if (b->len == b->cap) {
        size_t newcap = b->cap == 0 ? 16 : b->cap * 2;
        uint32_t *n = realloc(b->data, newcap * sizeof(uint32_t));
        if (!n) abort();
        b->data = n;
        b->cap = newcap;
    }
    b->data[b->len++] = cp;
}

static CpBuf utf8_to_codepoints(const uint8_t *s, size_t len) {
    CpBuf out;
    out.data = NULL;
    out.len = 0;
    out.cap = 0;
    size_t i = 0;
    while (i < len) {
        uint8_t b0 = s[i];
        uint32_t cp;
        size_t n;
        if ((b0 & 0x80) == 0) {
            cp = b0;
            n = 1;
        } else if ((b0 & 0xe0) == 0xc0 && i + 1 < len) {
            cp = (uint32_t)(b0 & 0x1f) << 6 | (s[i + 1] & 0x3f);
            n = 2;
        } else if ((b0 & 0xf0) == 0xe0 && i + 2 < len) {
            cp = (uint32_t)(b0 & 0x0f) << 12 | (uint32_t)(s[i + 1] & 0x3f) << 6 | (s[i + 2] & 0x3f);
            n = 3;
        } else if ((b0 & 0xf8) == 0xf0 && i + 3 < len) {
            cp = (uint32_t)(b0 & 0x07) << 18 | (uint32_t)(s[i + 1] & 0x3f) << 12 |
                 (uint32_t)(s[i + 2] & 0x3f) << 6 | (s[i + 3] & 0x3f);
            n = 4;
        } else {
            /* Malformed: shouldn't happen (see comment above); skip 1 byte
             * defensively rather than reading out of bounds. */
            cp = 0xfffd;
            n = 1;
        }
        cpbuf_push(&out, cp);
        i += n;
    }
    return out;
}

static void append_utf8(uint8_t **buf, size_t *len, size_t *cap, uint32_t cp) {
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
        size_t newcap = (*cap == 0 ? 16 : *cap * 2);
        while (newcap < *len + n) newcap *= 2;
        uint8_t *nb = realloc(*buf, newcap);
        if (!nb) abort();
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, tmp, n);
    *len += n;
}

/* --- table lookups (binary search; tables are sorted by codepoint / by
 * (cp1, cp2), see gen_nfc_tables.py) --- */

static uint8_t combining_class(uint32_t cp) {
    size_t lo = 0, hi = nfc_combining_table_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (nfc_combining_table[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < nfc_combining_table_len && nfc_combining_table[lo].cp == cp) {
        return nfc_combining_table[lo].ccc;
    }
    return 0;
}

static const NfcDecompEntry *find_decomp(uint32_t cp) {
    size_t lo = 0, hi = nfc_decomp_table_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (nfc_decomp_table[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < nfc_decomp_table_len && nfc_decomp_table[lo].cp == cp) {
        return &nfc_decomp_table[lo];
    }
    return NULL;
}

static bool find_compose(uint32_t cp1, uint32_t cp2, uint32_t *out) {
    size_t lo = 0, hi = nfc_compose_table_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const NfcComposeEntry *e = &nfc_compose_table[mid];
        if (e->cp1 < cp1 || (e->cp1 == cp1 && e->cp2 < cp2))
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < nfc_compose_table_len && nfc_compose_table[lo].cp1 == cp1 && nfc_compose_table[lo].cp2 == cp2) {
        *out = nfc_compose_table[lo].composed;
        return true;
    }
    return false;
}

static void decompose_hangul(uint32_t r, CpBuf *out) {
    uint32_t s_index = r - HANGUL_SBASE;
    uint32_t l = HANGUL_LBASE + s_index / HANGUL_NCOUNT;
    uint32_t v = HANGUL_VBASE + (s_index % HANGUL_NCOUNT) / HANGUL_TCOUNT;
    uint32_t t = s_index % HANGUL_TCOUNT;
    cpbuf_push(out, l);
    cpbuf_push(out, v);
    if (t != 0) cpbuf_push(out, HANGUL_TBASE + t);
}

static bool compose_hangul(uint32_t a, uint32_t b, uint32_t *out) {
    if (a >= HANGUL_LBASE && a < HANGUL_LBASE + HANGUL_LCOUNT && b >= HANGUL_VBASE && b < HANGUL_VBASE + HANGUL_VCOUNT) {
        uint32_t l_index = a - HANGUL_LBASE;
        uint32_t v_index = b - HANGUL_VBASE;
        *out = HANGUL_SBASE + (l_index * HANGUL_VCOUNT + v_index) * HANGUL_TCOUNT;
        return true;
    }
    if (is_hangul_syllable(a) && (a - HANGUL_SBASE) % HANGUL_TCOUNT == 0 && b > HANGUL_TBASE && b < HANGUL_TBASE + HANGUL_TCOUNT) {
        *out = a + (b - HANGUL_TBASE);
        return true;
    }
    return false;
}

static CpBuf nfd_decompose(const uint32_t *cps, size_t n) {
    CpBuf out;
    out.data = NULL;
    out.len = 0;
    out.cap = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t r = cps[i];
        if (is_hangul_syllable(r)) {
            decompose_hangul(r, &out);
            continue;
        }
        const NfcDecompEntry *d = find_decomp(r);
        if (d) {
            for (uint32_t j = 0; j < d->len; j++) {
                cpbuf_push(&out, nfc_decomp_pool[d->offset + j]);
            }
        } else {
            cpbuf_push(&out, r);
        }
    }
    /* Stable insertion sort of each maximal non-starter run by combining
     * class -- runs are short in practice, matching the Go port. */
    size_t i = 0;
    while (i < out.len) {
        if (combining_class(out.data[i]) == 0) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < out.len && combining_class(out.data[j]) != 0) j++;
        for (size_t k = i + 1; k < j; k++) {
            uint32_t v = out.data[k];
            uint8_t v_class = combining_class(v);
            size_t m = k;
            while (m > i && combining_class(out.data[m - 1]) > v_class) {
                out.data[m] = out.data[m - 1];
                m--;
            }
            out.data[m] = v;
        }
        i = j;
    }
    return out;
}

static CpBuf nfc_compose_runes(const uint32_t *decomposed, size_t n) {
    CpBuf out;
    out.data = NULL;
    out.len = 0;
    out.cap = 0;
    if (n == 0) return out;
    cpbuf_push(&out, decomposed[0]);
    size_t starter_idx = 0;
    uint8_t last_class = combining_class(decomposed[0]);
    last_class = (last_class != 0) ? 255 : 0;

    for (size_t idx = 1; idx < n; idx++) {
        uint32_t ch = decomposed[idx];
        uint8_t ch_class = combining_class(ch);
        uint32_t composed = 0;
        bool ok = false;
        if (last_class == 0 || last_class < ch_class) {
            uint32_t base = out.data[starter_idx];
            if (find_compose(base, ch, &composed)) {
                ok = true;
            } else if (compose_hangul(base, ch, &composed)) {
                ok = true;
            }
        }
        if (ok) {
            out.data[starter_idx] = composed;
            continue;
        }
        cpbuf_push(&out, ch);
        if (ch_class == 0) {
            starter_idx = out.len - 1;
            last_class = 0;
        } else {
            last_class = ch_class;
        }
    }
    return out;
}

uint8_t *nfc_normalize_utf8(const uint8_t *utf8, size_t len, size_t *out_len) {
    CpBuf cps = utf8_to_codepoints(utf8, len);
    CpBuf decomposed = nfd_decompose(cps.data, cps.len);
    free(cps.data);
    CpBuf composed = nfc_compose_runes(decomposed.data, decomposed.len);
    free(decomposed.data);

    uint8_t *out = NULL;
    size_t olen = 0, ocap = 0;
    for (size_t i = 0; i < composed.len; i++) {
        append_utf8(&out, &olen, &ocap, composed.data[i]);
    }
    free(composed.data);
    if (!out) {
        out = malloc(1);
        if (!out) abort();
    }
    *out_len = olen;
    return out;
}
