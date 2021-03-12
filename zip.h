#ifndef _ZIP_H
#define _ZIP_H

#include "ebuf.h"

/* Extract file from zip-archive */
int zip_extract(const char *zip, const char *fname,
		int (*wr)(const char *, int, void *), void *wr_priv,
		struct ebuf *ebuf);

#endif

