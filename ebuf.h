#ifndef _EBUF_H
#define _EBUF_H

struct ebuf {
	char *buf;
	char *end;
	char *p;
};

void ebuf_init(struct ebuf *ebuf, char *buf, int sz);

void ebuf_add(struct ebuf *ebuf, const char *frmt, ...);

void ebuf_clr(struct ebuf *ebuf);

const char *ebuf_s(struct ebuf *ebuf);

#endif
