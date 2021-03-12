#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ods.h"

static int col_letter2num(int c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	return -1;
}

int main(int argc, char *argv[])
{
	struct ebuf ebuf;
	char ebuf_buf[1024];
	void *ctx, *sheet_ctx;
	int r = -1;
	int i, j;
	char c1, c2;
	int row1, col1, row2, col2;
	const char *s;

	if (argc != 4) {
		fprintf(stderr, "Read values from Open Document Spreadsheet files (.ods):\nUsage: <ods-file> <sheet> B1:H99\n");
		return -1;
	}

	if (sscanf(argv[3], "%c%d:%c%d", &c1, &row1, &c2, &row2) != 4) {
		fprintf(stderr, "Invalid area format\n");
		return -1;
	}

	col1 = col_letter2num(c1);
	col2 = col_letter2num(c2);
	if (col1 < 0 || col2 < 0) {
		fprintf(stderr, "Invalid column name\n");
		return -1;
	}

	if (col1 > col2 || row1 > row2) {
		fprintf(stderr, "Invalid area coordinates\n");
		return -1;
	}

	row1--;
	row2--;

	ebuf_init(&ebuf, ebuf_buf, sizeof(ebuf_buf));

	ctx = ods_open(argv[1], &ebuf);
	if (!ctx) {
		printf("%s", ebuf_s(&ebuf));
		return -1;
	}

	sheet_ctx = ods_open_sheet(ctx, argv[2], &ebuf);
	if (!sheet_ctx) {
		fprintf(stderr, "%s", ebuf_s(&ebuf));
		goto fin;
	}

	for (i = row1; i <= row2; i++) {
		for (j = col1; j <= col2; j++) {
			printf("\t%s", ods_sheet_val(sheet_ctx, i, j));
		}
		printf("\n");
	}
	printf("\n");

	ods_close_sheet(sheet_ctx);

	r = 0;

fin:
	ods_close(ctx);
	return r;
}
