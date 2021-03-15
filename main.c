#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ods.h"

static int parse_cell_name(const char *s, int *row, int *col)
{
	char c;
	int n;

	if (sscanf(s, "%c%u%n", &c, row, &n) != 2)
		return -1;

	if (c < 'A' || c > 'Z' || row <= 0)
		return -1;
	*col = c - 'A';
	(*row)--;

	return n;
}

struct cell_area {
	int row1, col1;
	int row2, col2;
};

static int parse_cell_area(const char *s, struct cell_area *area)
{
	char c;
	int n, row, col;

	n = parse_cell_name(s, &area->row1, &area->col1);
	if (n < 0)
		return -1;

	if (!s[n]) {
		area->row2 = area->row1;
		area->col2 = area->col1;
		return 0;
	}

	if (s[n] != ':')
		return -1;

	s += n + 1;

	n = parse_cell_name(s, &area->row2, &area->col2);
	if (n < 0 || s[n])
		return -1;

	return 0;
}

int main(int argc, char *argv[])
{
	struct ebuf ebuf;
	char ebuf_buf[1024];
	void *ctx, *sheet_ctx;
	int i, j;
	struct cell_area ca;
	const char *s;
	const char *fname, *sheet = NULL, *area = NULL;

	if (argc < 2 || argc > 4) {
		fprintf(stderr, "Read values from Open Document Spreadsheet files (.ods):\nUsage: <ods-file> [<sheet> [B1[:H99]]]\n");
		return -1;
	}

	fname = argv[1];

	if (argc > 2)
		sheet = argv[2];

	if (argc > 3) {
		area = argv[3];
		if (parse_cell_area(area, &ca)) {
			fprintf(stderr, "Invalid cell area format\n");
			return -1;
		}
	}

	ebuf_init(&ebuf, ebuf_buf, sizeof(ebuf_buf));

	ctx = ods_open(fname, &ebuf);
	if (!ctx) {
		fprintf(stderr, "%s", ebuf_s(&ebuf));
		return -1;
	}

	if (!sheet) {
		ods_print_sheet_names(ctx);
		return 0;
	}

	if (!area)
		return ods_print_sheet(ctx, sheet);

	sheet_ctx = ods_open_sheet(ctx, sheet, &ebuf);
	if (!sheet_ctx) {
		fprintf(stderr, "%s", ebuf_s(&ebuf));
		return -1;
	}

	for (i = ca.row1; i <= ca.row2; i++) {
		for (j = ca.col1; j <= ca.col2; j++) {
			printf("\t%s", ods_sheet_val(sheet_ctx, i, j));
		}
		printf("\n");
	}
	printf("\n");

	ods_close_sheet(sheet_ctx);

	ods_close(ctx);

	return 0;
}
