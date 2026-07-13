/* CLI entry point + batch stdin/stdout protocol (CONTRIBUTING.md's adapter
 * contract), ported from adapters/go/main.go (itself cross-checked against
 * adapters/rust, adapters/kotlin, adapters/typescript). */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_error.h"
#include "dcbor.h"
#include "decode.h"
#include "logical_value.h"
#include "rfc8949.h"
#include "util.h"

static const char *parse_profile_arg(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
    }
    return NULL;
}

/* Reads one line (delimited by '\n', tolerating a trailing '\r') from
 * stdin into a growable buffer. Returns 1 on a line read (possibly empty),
 * 0 on clean EOF with nothing read, -1 on a read error. Blank lines
 * (post-trim) are the caller's concern, not this function's -- mirrors
 * adapters/go/main.go's forEachLine, which skips blank lines itself. */
static int read_line(StrBuf *line) {
    line->len = 0;
    if (line->cap > 0) line->data[0] = '\0';
    bool got_any = false;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        got_any = true;
        if (c == '\n') break;
        strbuf_append_char(line, (char)c);
    }
    if (!got_any) {
        if (feof(stdin) && !ferror(stdin)) return 0;
        if (ferror(stdin)) return -1;
        return 0;
    }
    if (ferror(stdin)) return -1;
    /* Trim a trailing '\r' (CRLF input) and any other trailing whitespace,
     * matching strings.TrimSpace in the Go adapter. */
    while (line->len > 0) {
        char last = line->data[line->len - 1];
        if (last == '\r' || last == ' ' || last == '\t') {
            line->len--;
            line->data[line->len] = '\0';
        } else {
            break;
        }
    }
    /* Trim leading whitespace too. */
    size_t start = 0;
    while (start < line->len && (line->data[start] == ' ' || line->data[start] == '\t')) start++;
    if (start > 0) {
        memmove(line->data, line->data + start, line->len - start + 1);
        line->len -= start;
    }
    return 1;
}

static bool print_line(const char *s) {
    if (fputs(s, stdout) == EOF) return false;
    if (fputc('\n', stdout) == EOF) return false;
    if (fflush(stdout) == EOF) return false;
    return true;
}

static int run_encode(const char *profile) {
    bool (*encoder)(const LogicalValue *, ByteBuf *);
    if (profile == NULL) {
        fprintf(stderr, "--profile is required\n");
        return 2;
    } else if (strcmp(profile, "rfc8949") == 0) {
        encoder = encode_rfc8949;
    } else if (strcmp(profile, "dcbor") == 0) {
        encoder = encode_dcbor;
    } else {
        fprintf(stderr, "unsupported profile: %s\n", profile);
        return 3;
    }

    bool had_error = false;
    bool io_error = false;
    StrBuf line;
    strbuf_init(&line);
    for (;;) {
        int rc = read_line(&line);
        if (rc < 0) {
            io_error = true;
            break;
        }
        if (rc == 0) break;
        if (line.len == 0) continue;

        char *parse_err = NULL;
        LogicalValue *logical = parse_logical_value_line(line.data, line.len, &parse_err);
        if (!logical) {
            fprintf(stderr, "malformed input line: %s\n", parse_err ? parse_err : "(unknown)");
            free(parse_err);
            had_error = true;
            if (!print_line("")) {
                io_error = true;
                break;
            }
            continue;
        }

        ByteBuf out;
        bytebuf_init(&out);
        if (!encoder(logical, &out)) {
            fprintf(stderr, "encode rejected: %s\n", adapter_last_error());
            had_error = true;
            logical_value_free(logical);
            bytebuf_free(&out);
            if (!print_line("")) {
                io_error = true;
                break;
            }
            continue;
        }
        logical_value_free(logical);
        char *hex = hex_encode(out.data, out.len);
        bytebuf_free(&out);
        bool ok = print_line(hex);
        free(hex);
        if (!ok) {
            io_error = true;
            break;
        }
    }
    strbuf_free(&line);

    if (io_error) {
        fprintf(stderr, "internal adapter error: stdin/stdout failure\n");
        return 2;
    }
    if (had_error) return 1;
    return 0;
}

static int run_decode_strict(const char *profile) {
    Profile decode_profile;
    if (profile == NULL) {
        fprintf(stderr, "--profile is required\n");
        return 2;
    } else if (strcmp(profile, "rfc8949") == 0) {
        decode_profile = PROFILE_RFC8949;
    } else if (strcmp(profile, "dcbor") == 0) {
        decode_profile = PROFILE_DCBOR;
    } else {
        fprintf(stderr, "unsupported profile: %s\n", profile);
        return 3;
    }

    bool had_error = false;
    bool io_error = false;
    StrBuf line;
    strbuf_init(&line);
    for (;;) {
        int rc = read_line(&line);
        if (rc < 0) {
            io_error = true;
            break;
        }
        if (rc == 0) break;
        if (line.len == 0) continue;

        uint8_t *bytes;
        size_t nbytes;
        if (!hex_decode(line.data, line.len, &bytes, &nbytes)) {
            fprintf(stderr, "malformed input line: invalid hex string\n");
            had_error = true;
            if (!print_line("")) {
                io_error = true;
                break;
            }
            continue;
        }

        Verdict verdict;
        bool ok = decode_strict(bytes, nbytes, decode_profile, &verdict);
        free(bytes);
        if (!ok) {
            fprintf(stderr, "decode-strict internal error: %s\n", adapter_last_error());
            had_error = true;
            if (!print_line("")) {
                io_error = true;
                break;
            }
            continue;
        }

        bool print_ok;
        if (verdict.accept) {
            char *hex = hex_encode(verdict.bytes.data, verdict.bytes.len);
            bytebuf_free(&verdict.bytes);
            StrBuf out;
            strbuf_init(&out);
            strbuf_append_str(&out, "ACCEPT ");
            strbuf_append_str(&out, hex);
            free(hex);
            print_ok = print_line(out.data ? out.data : "");
            strbuf_free(&out);
        } else {
            bytebuf_free(&verdict.bytes);
            StrBuf out;
            strbuf_init(&out);
            strbuf_append_str(&out, "REJECT ");
            strbuf_append_str(&out, verdict.reason);
            print_ok = print_line(out.data ? out.data : "");
            strbuf_free(&out);
        }
        if (!print_ok) {
            io_error = true;
            break;
        }
    }
    strbuf_free(&line);

    if (io_error) {
        fprintf(stderr, "internal adapter error: stdin/stdout failure\n");
        return 2;
    }
    if (had_error) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: adapter <mode> --profile <profile>\n");
        return 2;
    }
    const char *profile = parse_profile_arg(argc - 2, argv + 2);

    if (strcmp(argv[1], "encode") == 0) {
        return run_encode(profile);
    }
    if (strcmp(argv[1], "decode-strict") == 0) {
        return run_decode_strict(profile);
    }
    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
