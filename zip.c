#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <zlib.h>

#include "zip.h"

/* Central Directory Header Signature */
#define CDHDR_SIG 0x02014b50
/* Local File Header Signature */
#define LFHDR_SIG 0x04034b50
/* End-Of-Central-Directory-Record Signature */
#define EOCDR_SIG 0x06054b50

#define COMPRESSION_METHOD_NONE    0x0000
#define COMPRESSION_METHOD_DEFLATE 0x0008

/* Local File Header */
struct lfhdr {
	uint32_t sig;
	uint16_t vers_needed_to_extract;
	uint16_t flags;
	uint16_t compression_method;
	uint16_t last_modif_time;
	uint16_t last_modif_date;
	uint32_t crc32;
	uint32_t compressed_sz;
	uint32_t uncompressed_sz;
	uint16_t fname_len;
	uint16_t extra_field_len;
} __attribute__ ((packed));

/*
struct data_descr {
	uint32_t crc32;
	uint32_t compressed_sz;
	uint32_t uncompressed_sz;
} __attribute__ ((packed));
*/

/* Central Directory Header */
struct cdhdr {
	uint32_t sig;
	uint16_t vers_made_by;
	uint16_t vers_needed_to_extract;
	uint16_t flags;
	uint16_t compression_method;
	uint16_t last_modif_time;
	uint16_t last_modif_date;
	uint32_t crc32;
	uint32_t compressed_sz;
	uint32_t uncompressed_sz;
	uint16_t fname_len;
	uint16_t extra_field_len;
	uint16_t comment_len;
	uint16_t disk_number_start;
	uint16_t internal_file_attr;
	uint32_t external_file_attr;
	uint32_t lfhdr_off;
} __attribute__ ((packed));

/* End-Of-Central-Directory Record */
struct eocdr {
	uint32_t sig;
	uint16_t cur_disk;
	uint16_t central_dir_start_disk;
	uint16_t nentries;
	uint16_t nentries_total;
	uint32_t central_dir_sz;
	uint32_t central_dir_off;
	uint16_t comment_sz;
} __attribute__ ((packed));

#define READ_EOCDR_BUF 4096
static int read_eocdr(FILE *fp, struct eocdr *r, struct ebuf *ebuf)
{
	int n;
	char buf[READ_EOCDR_BUF];
	char *p;

        /* Suppose, that EOCDR is in the last N bytes */

	if (fseek(fp, -READ_EOCDR_BUF-1, SEEK_END)) {
		ebuf_add(ebuf, "zip: failed to seek while looking for End-Of-Central-Dir Record: %s\n", strerror(errno));
		return -1;
	}

	n = fread(buf, 1, READ_EOCDR_BUF, fp);
	if (n <= 0) {
		ebuf_add(ebuf, "zip: failed to read the End-Of-Central-Dir Record: %s\n", strerror(errno));
		return -1;
	}

	p = buf-1;
	do {
		p++;
		/* Find the first byte of EOCDR signature */
		p = memchr(p, 0x50, buf + n - p);
	} while (p && (buf + n - p >= 4) && *(uint32_t *)p != EOCDR_SIG);

	if (!p || (buf + n - p < 4)) {
		ebuf_add(ebuf, "zip: failed to found the End-Of-Central-Dir Record\n");
		return -1;
	}

	memcpy(r, p, sizeof(*r));

	if (r->nentries != r->nentries_total ||
		r->cur_disk != r->central_dir_start_disk) {
		       ebuf_add(ebuf, "zip: unexpected values in the End-Of-Central-Dir Record\n");
		       return -1;
	}

	return 0;
}

static int ls_central_dir(FILE *fp, struct eocdr *eocdr, struct ebuf *ebuf,
        int (*f)(FILE *, struct cdhdr *, const char *, int *, void *), void
        *f_priv)
{
	struct cdhdr cdhdr;
	const char *p;
	int fnlen, nentries = (int)eocdr->nentries_total, fin;
	char fname[256];
	long cur_pos;

	if (fseek(fp, (long)eocdr->central_dir_off, SEEK_SET)) {
		ebuf_add(ebuf, "zip: failed to seek to the Central Dir: %s\n", strerror(errno));
		return -1;
	}

	fin = 0;
	while (nentries-- && !fin) {
		if (fread(&cdhdr, sizeof(cdhdr), 1, fp) != 1) {
			ebuf_add(ebuf, "zip: failed to read Central Dir header: %s\n", strerror(errno));
			return -1;
		}

		if (cdhdr.sig != CDHDR_SIG) {
			ebuf_add(ebuf, "zip: invalid Central Dir header signature\n");
			return -1;
		}

		fnlen = cdhdr.fname_len;

		if (fnlen > sizeof(fname) - 1) {
			ebuf_add(ebuf, "zip: too long fname in Central Dir header: %d\n", fnlen);
			return -1;
		}

		if (fread(fname, fnlen, 1, fp) != 1) {
			ebuf_add(ebuf, "zip: failed to read Central Dir fname: %s\n", strerror(errno));
			return -1;
		}

		fname[fnlen] = '\0';

		cur_pos = ftell(fp);

		if (f(fp, &cdhdr, fname, &fin, f_priv))
			return -1;

		cur_pos += cdhdr.extra_field_len + cdhdr.comment_len;

		if (fseek(fp, cur_pos, SEEK_SET)) {
			ebuf_add(ebuf, "zip: failed to seek to the next Central Dir header: %s\n", strerror(errno));
			return -1;
		}
	}

	return 0;
}

struct extract_ctx {
	const char *fname;
	int (*wr)(const char *, int, void *);
	void *wr_priv;
	int found;
	struct ebuf *ebuf;
};

static int decompress(FILE *in, struct cdhdr *cdhdr, struct extract_ctx *ctx)
{
	char ibuf[1024];
	char obuf[1024];
	int r, nleft, err = -1;
	z_stream zs;
	unsigned long crc;

	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	zs.avail_in = 0;
	zs.next_in = Z_NULL;
	r = inflateInit2(&zs, -15);
	if (r != Z_OK) {
		ebuf_add(ctx->ebuf, "zip: failed to init zlib inflate stream: %d", r);
		return -1;
	}

	for (nleft = cdhdr->compressed_sz, crc = crc32(0L, Z_NULL, 0); nleft && r != Z_STREAM_END;) {
		zs.avail_in = fread(ibuf, 1, nleft > sizeof(ibuf) ? sizeof(ibuf) : nleft, in);
		if (ferror(in)) {
			ebuf_add(ctx->ebuf, "zip: failed to read: %s\n", strerror(errno));
			goto fin;
		}

		if (zs.avail_in == 0) {
			ebuf_add(ctx->ebuf, "zip: read unexpected EOF while decompressing\n");
			goto fin;
		}

		nleft -= zs.avail_in;

		zs.next_in = ibuf;
		do {
			zs.avail_out = sizeof(obuf);
			zs.next_out = obuf;
			r = inflate(&zs, Z_NO_FLUSH);
			if (r == Z_NEED_DICT || r == Z_DATA_ERROR ||
				r == Z_MEM_ERROR) {
				ebuf_add(ctx->ebuf, "zip: zlib inflate failed: %d\n", r);
				goto fin;
			}

			if (ctx->wr(obuf, sizeof(obuf) - zs.avail_out, ctx->wr_priv)) {
				ebuf_add(ctx->ebuf, "zip: failed to write decompressed data\n");
				goto fin;
			}

			crc = crc32(crc, obuf, sizeof(obuf) - zs.avail_out);
		} while (zs.avail_out == 0);
	}

	if (r != Z_STREAM_END) {
		ebuf_add(ctx->ebuf, "zip: unexpected end of compressed data\n");
		goto fin;
	}

	if (nleft) {
		ebuf_add(ctx->ebuf, "zip: unexpected end of zlib inflate stream\n");
		goto fin;
	}

	if (crc != cdhdr->crc32) {
		ebuf_add(ctx->ebuf, "zip: CRC mismatch\n");
		goto fin;
	}

	err = 0;

fin:
	inflateEnd(&zs);
	return err;
}

static int extract(FILE *fp, struct cdhdr *cdhdr, const char *fname, int *fin,
		   void *priv)
{
	struct lfhdr lfhdr;
	int nleft, n;
	unsigned long crc;
	struct extract_ctx *ctx = (struct extract_ctx *)priv;
	char buf[256];

	if (strcmp(fname, ctx->fname))
		return 0;

	ctx->found = 1;
	*fin = 1;

	if (fseek(fp, cdhdr->lfhdr_off, SEEK_SET)) {
		ebuf_add(ctx->ebuf, "zip: failed to seek to Local File header off=%ld: %s\n",
			(long)cdhdr->lfhdr_off, strerror(errno));
		return -1;
	}

	if (fread(&lfhdr, sizeof(lfhdr), 1, fp) != 1) {
		ebuf_add(ctx->ebuf, "zip: failed to read Local File header: %s\n",
			strerror(errno));
		return -1;
	}

	if (lfhdr.sig != LFHDR_SIG) {
		ebuf_add(ctx->ebuf, "zip: invalid Local File header signature\n");
		return -1;
	}

	/* TODO: compare CDHDR to LFHDR */

	if (fseek(fp, lfhdr.fname_len + lfhdr.extra_field_len, SEEK_CUR)) {
		ebuf_add(ctx->ebuf, "zip: failed to seek to file data: %s\n",
			strerror(errno));
		return -1;
	}

	if (lfhdr.compression_method) {
		if (lfhdr.compression_method == COMPRESSION_METHOD_DEFLATE) {
			if (decompress(fp, cdhdr, ctx))
				return -1;

			goto fin;
		}

		ebuf_add(ctx->ebuf, "zip: file compression method is not deflate\n");
		return -1;
	}

	/* No compression */

	crc = crc32(0L, Z_NULL, 0);
	for (nleft = lfhdr.compressed_sz; nleft; nleft -= n) {
		n = fread(buf, 1, nleft > sizeof(buf) ? sizeof(buf) : nleft, fp);
		if (n <= 0) {
			ebuf_add(ctx->ebuf, "zip: failed to read: %s\n",
				strerror(errno));
			return -1;
		}

		if (ctx->wr(buf, n, ctx->wr_priv)) {
			ebuf_add(ctx->ebuf, "zip: failed to write extracted data\n");
			return -1;
		}
		crc = crc32(crc, (unsigned char *)buf, n);
	}

	if (lfhdr.crc32 != crc) {
		ebuf_add(ctx->ebuf, "zip: extracted file CRC mismatch\n");
		return -1;
	}


fin:
	if (ctx->wr(NULL, 0, ctx->wr_priv)) {
		ebuf_add(ctx->ebuf, "zip: failed to write extracted extracted data: %s\n");
		return -1;
	}

	return 0;
}

int zip_extract(const char *zip, const char *fname,
		int (*wr)(const char *, int, void *), void *wr_priv,
		struct ebuf *ebuf)
{
	struct eocdr eocdr;
	FILE *fp;
	int r = -1;
	struct extract_ctx ctx;

	fp = fopen(zip, "rb+");
	if (!fp) {
		ebuf_add(ebuf, "zip: failed to open zip-file: %s\n",
			strerror(errno));
		return -1;
	}

	if (read_eocdr(fp, &eocdr, ebuf))
		goto fin;

	ctx.fname = fname;
	ctx.wr = wr;
	ctx.wr_priv = wr_priv;
	ctx.ebuf = ebuf;
	ctx.found = 0;
	r = ls_central_dir(fp, &eocdr, ebuf, extract, &ctx);
	if (!r) {
		if (!ctx.found) {
			ebuf_add(ebuf, "zip: file not found\n");
			r = -1;
		}
	}

fin:
	fclose(fp);
	return r;
}

#ifdef ZIP_MAIN

struct extr_wr_ctx {
	FILE *fp;
	struct ebuf *ebuf;
};

static int extr_wr(const char *buf, int n, void *priv)
{
	struct extr_wr_ctx *ctx = (struct extr_wr_ctx *)priv;

	if (!n) { /* End of data? */
		if (fflush(ctx->fp))
			goto wr_err;
	} else {
		if (fwrite(buf, n, 1, ctx->fp) != 1)
			goto wr_err;
	}

	return 0;

wr_err:
	ebuf_add(ctx->ebuf, "zip: failed to write to file: %s\n",
		strerror(errno));
	return -1;
}

int main(int argc, char *argv[])
{
	struct ebuf ebuf;
	char ebuf_buf[256];
	FILE *fp;
	struct extr_wr_ctx ewr_ctx;

	if (argc != 4) {
		fprintf(stderr, "Extract file from zip-archive.\n Usage: <zip-file> <fname> <to>\n");
		return -1;
	}

	fp = fopen(argv[3], "wb");
	if (!fp) {
		fprintf(stderr, "Failed to create output file: %s\n",
			strerror(errno));
		return -1;
	}

	ebuf_init(&ebuf, ebuf_buf, sizeof(ebuf_buf));

	ewr_ctx.fp = fp;
	ewr_ctx.ebuf = &ebuf;
	if (zip_extract(argv[1], argv[2], extr_wr, &ewr_ctx, &ebuf)) {
		fprintf(stderr, "%s\n", ebuf_s(&ebuf));
		fclose(fp);
		return -1;
	}

	fclose(fp);

	printf("OK\n");

	return 0;
}

#endif

