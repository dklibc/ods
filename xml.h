#ifndef _XML_H
#define _XML_H

#include <stdio.h>

#include "ebuf.h"

enum xml_elem_type {
	XML_ELEM_TYPE_UNDEF = 0, /* Undefined type */
	XML_ELEM_TYPE_ELEM  = 1,
	XML_ELEM_TYPE_EMPTY = 2, /* Empty-element tag */
	XML_ELEM_TYPE_TEXT  = 3,
};

struct xml_attr {
	char *name;
	char *val;
	struct xml_attr *pnext;
};

struct xml_elem {
	enum xml_elem_type type;
	char *name; /* Text for text element */
	struct xml_elem *parent; /* Null for root element */
	struct xml_elem *child;  /* List of childs. Can be null */
	struct xml_attr *attr;   /* List of attributes */

	struct xml_elem *pnext; /* In parent's childs list */
};

struct xml_elem *xml_parse(FILE *fp, struct ebuf *ebuf);

int xml_print(struct xml_elem *root, FILE *fp);

void xml_free(struct xml_elem *root);

struct xml_elem *xml_get_child(struct xml_elem *elem, const char *name);

struct xml_elem *xml_get_elem(struct xml_elem *root, const char *path);

char *xml_get_attr(struct xml_elem *elem, const char *name);

struct xml_elem *xml_get_child_with_attr(struct xml_elem *elem,
					 const char *name,
					 const char *attr,
					 const char *val);
#endif
