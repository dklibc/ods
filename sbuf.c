/*
 * String buffer
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sbuf.h"

void sbuf_init(struct sbuf *sbuf, char *buf, int buf_sz)
{
	sbuf->buf = sbuf->tail = buf;
	sbuf->buf_sz = buf_sz;
}

int sbuf_add(struct sbuf *sbuf, int c)
{
	if (sbuf->tail - sbuf->buf >= sbuf->buf_sz)
		return -1;

	*(sbuf->tail)++ = c;

	return 0;
}

char *sbuf_dup(struct sbuf *sbuf)
{
	char *s;
	int n = sbuf->tail - sbuf->buf;

	s = malloc(n + 1);
	if (!s) {
		fprintf(stderr, "%s: No memory!\n", __func__);
		return NULL;
	}

	memcpy(s, sbuf->buf, n);
	*(s + n) = '\0';

	/*
	 * WARNING: Automatically trash, since it is
	 * the very usual case.
	 */
	sbuf_trash(sbuf);

	return s;
}

void sbuf_trash(struct sbuf *sbuf)
{
	sbuf->tail = sbuf->buf;
}

char *sbuf_buf(struct sbuf *sbuf)
{
	return sbuf->buf;
}

char *sbuf_tail(struct sbuf *sbuf)
{
	return sbuf->tail;
}
