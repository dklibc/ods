#ifndef _SBUF_H
#define _SBUF_H

/* String buffer */
struct sbuf {
	char *buf;
	char *tail;
	int buf_sz;
};

void sbuf_init(struct sbuf *sbuf, char *buf, int buf_sz);

int sbuf_add(struct sbuf *sbuf, int c);

char *sbuf_dup(struct sbuf *sbuf);

void sbuf_trash(struct sbuf *sbuf);

char *sbuf_buf(struct sbuf *sbuf);

char *sbuf_tail(struct sbuf *sbuf);

#endif

