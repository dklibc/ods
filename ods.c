/*
 * Functions to work with Open Document Spreadsheets (*.ods files).
 * ods-file are actually zip-archives. Main data is in content.xml file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "xml.h"
#include "zip.h"
#include "ods.h"

#define SPREADSHEET_ELEM_PATH "/office:document-content/office:body/office:spreadsheet"

#define ROWS 150
#define COLS 26

struct ctx {
	struct xml_elem *root;
	struct xml_elem *spreadsheet;
};

struct sheet_ctx {
	struct ctx *ctx;
	const char *name;
	struct xml_elem *sheet;
	const char *val[ROWS][COLS];
};

struct zip_extr_wr_ctx {
	FILE *fp;
	struct ebuf *ebuf;
};

static int zip_extr_wr(const char *buf, int n, void *priv)
{
	struct zip_extr_wr_ctx *ctx = (struct zip_extr_wr_ctx *)priv;

	if (!n) { /* End of data? */
		if (fflush(ctx->fp))
			goto wr_err;
	} else {
		if (fwrite(buf, n, 1, ctx->fp) != 1)
			goto wr_err;
	}

	return 0;

wr_err:
	ebuf_add(ctx->ebuf, "ods: failed to write to a tmp file: %s\n", strerror(errno));
	return -1;
}

void *ods_open(const char *fname, struct ebuf *ebuf)
{
	struct xml_elem *root, *spreadsheet;
	struct ctx *ctx;
	struct zip_extr_wr_ctx wr_ctx;
	FILE *fp;

	/* Extract content.xml from ods-file to a temp file */
	fp = tmpfile();
	if (!fp) {
		ebuf_add(ebuf, "ods: failed to create a tmp file: %s\n",
			strerror(errno));
		return NULL;
	}
	wr_ctx.fp = fp;
	wr_ctx.ebuf = ebuf;
	if (zip_extract(fname, "content.xml", zip_extr_wr, &wr_ctx, ebuf)) {
		ebuf_add(ebuf, "ods: failed to extract \"content.xml\"\n");
		fclose(fp);
		return NULL;
	}

	if (fseek(fp, 0, SEEK_SET)) {
		ebuf_add(ebuf, "ods: failed to seek to the begining of the tmp file: %s\n", strerror(errno));
		fclose(fp);
		return NULL;
	}

	ctx = calloc(sizeof(*ctx), 1);
	if (!ctx) {
		ebuf_add(ebuf, "ods: no memory for spreadsheet ctx\n");
		fclose(fp);
		return NULL;
	}

	ctx->root = xml_parse(fp, ebuf);
	fclose(fp);
	if (!ctx->root) {
		ebuf_add(ebuf, "ods: failed to parse spreadsheet\n");
		goto err;
	}

	ctx->spreadsheet = xml_get_elem(ctx->root, SPREADSHEET_ELEM_PATH);
	if (!ctx->spreadsheet) {
		ebuf_add(ebuf, "ods: spreadsheet root elem not found\n");
		goto err;
	}

	return ctx;

err:
	xml_free(ctx->root);
	free(ctx);
	return NULL;
}

void ods_close(void *_ctx)
{
	struct ctx *ctx = (struct ctx *)_ctx;

	if (!ctx)
		return;

	xml_free(ctx->root);
}

/* Row and col args are only needed for output in error messages */
static const char *get_cell_val(struct xml_elem *cell, int row, int col,
				struct ebuf *ebuf)
{
	const char *type;
	struct xml_elem *p;
	char *s, *q;
	char buf[256];
	int n;

	type = xml_get_attr(cell, "office:value-type");
	if (!type)
		return NULL;

	/* For float cells we also return text value -- how user see it. */
	if (!strcmp(type, "string") || !strcmp(type, "float")) {
		p = cell->child;
		if (!p || p->pnext || strcmp(p->name, "text:p")) {
			ebuf_add(ebuf, "xml: expected \"text:p\" elem in the string cell (%d, %d)\n", row, col);
			return NULL;
		}

		p = p->child;
		if (!p) {
			s = "";
		} else if (!p->pnext && p->type == XML_ELEM_TYPE_TEXT) {
			s = p->name;
		} else { /* Impossible for float cells? */
			/* Concat text parts */
			q = buf;
			do {
				if (p->type == XML_ELEM_TYPE_TEXT) {
					n = strlen(p->name);
					if (q + n - buf > sizeof(buf) - 1) {
						ebuf_add(ebuf, "xml: too long text in (%d,%d)\n", row, col);
						return NULL;
					}
					memcpy(q, p->name, n + 1);
					q += n;
				}
				p = p->pnext;
			} while (p);
			s = buf;
		}
	} /*else if (!strcmp(type, "float")) {
		s = xml_get_attr(cell, "office:value");
	}*/ else { /* Unknown cell type */
		return NULL;
	}

	s = strdup(s);
	if (!s)
		ebuf_add(ebuf, "xml: no memory for cell value copy\n");

	return s;
}

static void handle_row(struct xml_elem *row, const char **val, int nrow,
			struct ebuf *ebuf)
{
	struct xml_elem *p;
	int i, n;
	const char *s;

	//printf("Handle row %d\n", nrow);
	for (p = row->child, i = 0; p && i < COLS; p = p->pnext) {
		if (!strcmp(p->name, "table:table-cell")
		    || !strcmp(p->name, "table:covered-table-cell")) {
			s = xml_get_attr(p, "table:number-columns-repeated");
			n = s ? atoi(s) : 1; /* Number of columns with the same value */
			if (i + n > COLS)
				n = COLS - i;
			s = get_cell_val(p, nrow, i, ebuf);
			//printf("Get col (%d, %d) val: \"%s\"\n", i, nrow, s);
			//printf("%d cols has the same value\n", n);
			i += n;
			while (n--)
				*val++ = s;
		}
	}
}

void *ods_open_sheet(void *_ctx, const char *name, struct ebuf *ebuf)
{
	struct ctx *ctx = (struct ctx *)_ctx;
	struct xml_elem *sheet, *p, *q;
	struct sheet_ctx *sh_ctx;
	int i, n;
	const char *s;

	sheet = xml_get_child_with_attr(ctx->spreadsheet, "table:table",
				        "table:name", name);
	if (!sheet) {
		ebuf_add(ebuf, "ods: sheet not found\n");
		return NULL;
	}

	sh_ctx = calloc(sizeof(*sh_ctx), 1);
	if (!sh_ctx) {
		ebuf_add(ebuf, "ods: no memory for sheet ctx\n");
		return NULL;
	}

	sh_ctx->ctx = ctx;

	sh_ctx->name = strdup(name);
	if (!sh_ctx->name) {
		ebuf_add(ebuf, "ods: No memory for sheet name\n");
		free(sh_ctx);
		return NULL;
	}

	sh_ctx->sheet = sheet;

	for (i = 0, p = sheet->child, q = NULL; p && i < ROWS;) {
		if (!strcmp(p->name, "table:table-row")) {
			handle_row(p, sh_ctx->val[i], i, ebuf);
			s = xml_get_attr(p, "table:number-rows-repeated");
			n = s ? atoi(s) : 1;
			if (i + n > ROWS)
				n = ROWS - i;
			while (--n) {
				memcpy(sh_ctx->val[i + 1], sh_ctx->val[i],
					COLS * sizeof(void *));
				i++;
			}

			i++;
		} else if (!q && !strcmp(p->name, "table:table-header-rows")) {
			q = p;
		}

		p = (p != q) ? p->pnext : p->child;

		if (!p && q) {
			p = q->pnext;
			q = NULL;
		}
	}

	return sh_ctx;
}

void ods_close_sheet(void *sheet_ctx)
{
	struct sheet_ctx *ctx = (struct sheet_ctx *)sheet_ctx;
	int i, j;
	const char **v, *p;

	if (!ctx)
		return;

	/*
	 * There can be a sequence of rows that are copy of each other.
	 * There can be a sequence of cols with the same pointer.
	 */
	for (i = 0, v = ctx->val[0]; i < ROWS; i++) {
		if (!i || memcmp(v, v - COLS, COLS * sizeof(*v))) {
			for (j = 0, p = NULL; j < COLS; j++, v++) {
				if (*v != p) {
					p = *v;
					free((void *)p);
				}
			}
		}
	}

	free((void *)ctx->name);
	free(ctx);
}

const char *ods_sheet_val(void *sheet_ctx, int row, int col)
{
	if (row < 0 || row >= ROWS || col < 0 || col >= COLS)
		return NULL;

	return ((struct sheet_ctx *)sheet_ctx)->val[row][col];
}

void ods_print_sheet_names(void *_ctx)
{
	struct ctx *ctx = (struct ctx *)_ctx;
	struct xml_elem *p;
	char *tname;

	for (p = ctx->spreadsheet->child; p; p = p->pnext) {
		if (!strcmp(p->name, "table:table")) {
			tname = xml_get_attr(p, "table:name");
			printf("%s\n", tname);
		}
	}
}

int ods_print_sheet(void *_ctx, const char *name)
{
	struct ctx *ctx = (struct ctx *)_ctx;
	struct xml_elem *sheet;

	sheet = xml_get_child_with_attr(ctx->spreadsheet, "table:table",
				        "table:name", name);
	if (!sheet) {
		fprintf(stderr, "Failed to get sheet \"%s\"\n", name);
		return -1;
	}

	xml_print(sheet, stdout);

	return 0;
}
