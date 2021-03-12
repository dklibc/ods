#ifndef _ODS_H
#define _ODS_H

#include "ebuf.h"

void *ods_open(const char *fname, struct ebuf *ebuf);

void ods_close(void *ctx);

void *ods_open_sheet(void *ctx, const char *name, struct ebuf *ebuf);

void ods_close_sheet(void *sheet_ctx);

const char *ods_sheet_val(void *ctx, int row, int col);

void ods_print_sheet_names(void *ctx);

void ods_print_sheet(void *ctx, const char *name);

#endif

