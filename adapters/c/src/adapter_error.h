#ifndef ADAPTER_ERROR_H
#define ADAPTER_ERROR_H

/* A single-threaded, single-slot "last error" diagnostic used across the
 * encode/decode paths. The batch CLI protocol only ever needs the *fact*
 * that a line failed (stdout gets an empty line / REJECT code); the
 * message text here only ever reaches stderr as a human diagnostic, so one
 * static buffer, overwritten per-line, is sufficient -- no need to thread
 * an allocated error string through every call site. */

void adapter_set_error(const char *fmt, ...);
const char *adapter_last_error(void);

#endif
