/*
 * Buffer for error messages.
 * Different modules can add error messages to it.
 * It is common practice to prefix message with module
 * short name. We see this buffer as basic analog of
 * Java stack trace.
 */

#include <stdarg.h>
#include <stdio.h>

#include "ebuf.h"

void ebuf_init(struct ebuf *ebuf, char *buf, int sz)
{
	ebuf->buf = ebuf->p = buf;
	ebuf->end = ebuf->buf + sz;
	*(ebuf->p) = '\0';
}

void ebuf_clr(struct ebuf *ebuf)
{
	ebuf->p = ebuf->buf;
	*(ebuf->p) = '\0';
}

void ebuf_add(struct ebuf *ebuf, const char *frmt, ...)
{
	va_list ap;
	int n, l;

	n = ebuf->end - ebuf->p - 1;

	va_start(ap, frmt);
	l = vsnprintf(ebuf->p, n, frmt, ap);
	va_end(ap);

	if (l > n)
		l = n;

	ebuf->p += l;

	if (*(ebuf->p - 1) != '\n') {
		*(ebuf->p) = '\n';
		*(ebuf->p + 1) = '\0';
		(ebuf->p) += 2;
	}
}

const char *ebuf_s(struct ebuf *ebuf)
{
	return ebuf->buf;
}
