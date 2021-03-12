#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#include "xml.h"
#include "stack.h"
#include "sbuf.h"

enum stat {
	STAT_UNDEF          = 0, /* Undefined state */

	/* STAG -- Start/opening tag */
	STAT_STAG           = 1,
	STAT_STAG_NAME      = 2,
	STAT_STAG_NAME_TAIL = 3,

	/* ETAG -- End/closing tag */
	STAT_ETAG           = 4,
	STAT_ETAG_NAME      = 5,
	STAT_ETAG_NAME_TAIL = 6,

	/* Attribute */
	STAT_ATTR_NAME      = 7,
	STAT_ATTR_NAME_TAIL = 8,
	STAT_ATTR_EQU       = 9,
	STAT_ATTR_VAL       = 10,
	STAT_ATTR_VAL_TAIL  = 11,

	/* Text */
	STAT_TEXT_TAIL      = 12,
	/* &lt; &amp; etc in text */
	STAT_TEXT_ESC       = 13,


	/* Misc states */

	/* XML Declaration */
	STAT_DECLAR         = 14,
	/* Start tag, end tag or text */
	STAT_TAG_OR_TEXT    = 15,
	/* Start or end tag */
	STAT_TAG            = 16,
	/* Comment */
	STAT_COMMENT        = 17,
	/* Empty-element tag */
	STAT_EETAG          = 18,
};

/* Check if it is valid char in tag/attribute name */
static int is_valid_name_char(int c, int first)
{
	if (c >= 'a' && c <= 'z'
	||  c >= 'A' && c <= 'Z'
	||  c == '_' || c == ':')
		return 1;

	if (first)
		return 0;

	if (c >= '0' && c <= '9'
	||  c == '.' || c == '-')
		return 1;

	return 0;
}

static int is_space(int c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

/* Check if it is valid char in escape sequence */
static int is_valid_esc_seq_char(int c)
{
	return c >= 'a' && c <= 'z';
}

/* Map escape sequence to the corresponding char */
static int escape_seq2char(char *p)
{
	if (!strcmp(p, "amp"))
		return '&';
	else if (!strcmp(p, "lt"))
		return '<';
	else if (!strcmp(p, "gt"))
		return '>';
	else if (!strcmp(p, "quot"))
		return '"';
	else if (!strcmp(p, "apos"))
		return '\'';
	else
		return -1;
}

/* Create a new element and add it to the existed tree */
static struct xml_elem *add_elem(char *name /* Save by pointer, not copy */,
				 enum xml_elem_type type,
				 struct xml_elem *parent,
				 struct xml_elem **prev)
{
	struct xml_elem *elem;

	if (parent && parent->child && !*prev) {
		fprintf(stderr, "%s: Bug: *prev must not be null",
			__func__);
		return NULL;
	}

	elem = calloc(sizeof(*elem), 1);
	if (!elem) {
		fprintf(stderr, "%s: No memory!\n", __func__);
		return NULL;
	}

	elem->type = type;
	elem->name = name;

	if (parent) {
		/* Add element to the end of parent's childs list */
		if (*prev)
			(*prev)->pnext = elem;
		else
			parent->child = elem;
		elem->parent = parent;

		*prev = elem;
	}

	return elem;
}

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))

struct xml_elem *xml_parse(FILE *fp, struct ebuf *ebuf)
{
	int c, quote = 0, xml_decl = 0;
	char *p, *s;
	int line = 0, pos = 0;
	int stat = STAT_STAG;
	struct xml_elem *elem = NULL, *parent = NULL, *prev = NULL,
		*root = NULL;
	struct xml_attr *attr = NULL, *prev_attr = NULL;
	/* Start tags stack -- used to match start and end tags */
	struct stack st_stack;
	void *st_buf[256];
	/* Previous child stack */
	struct stack pch_stack;
	void *pch_buf[256]; /* Size should be equal to st_buf */
	/* String buffer */
	struct sbuf sbuf;
	char sb_buf[256];
	char esc[32];

	stack_init(&st_stack, st_buf, ARRAY_LEN(st_buf));

	stack_init(&pch_stack, pch_buf, ARRAY_LEN(pch_buf));

	sbuf_init(&sbuf, sb_buf, sizeof(sb_buf));

	/* Main loop */
	while ((c = fgetc(fp)) != EOF) {
		pos++;

		if (c == '\n') {
			line++;
			pos = 0;
		}

#ifdef _XML_DBG
		printf("%c", c);
#endif

		switch (stat) {
			case STAT_DECLAR: /* Read the rest of XML declaration (after "<?") till "?>" */
				if (c == '>') {
					p = sbuf_buf(&sbuf);
					s = sbuf_tail(&sbuf);
					if (s - p < 4 || memcmp(p, "xml", 3) || *(s - 1) != '?') {
						ebuf_add(ebuf, "xml: invalid XML declaration\n");
						goto err;
					}
					sbuf_trash(&sbuf);

					xml_decl = 1;

					stat = STAT_STAG;
					break;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: too long XML declaration\n");
					goto err;
				}

				break;

			case STAT_STAG: /* Expect a start tag only (root) */
				if (is_space(c))
					break; /* Skip */

				if (c != '<') {
					ebuf_add(ebuf, "xml: %d:%d: Expected '<'. Get: '%c'(%02x)\n", line, pos, c, c);
					goto err;
				}

				stat = STAT_STAG_NAME;
				break;

			case STAT_STAG_NAME: /* Start tag name */
				if (c == '?' && !parent && !xml_decl) { /* "<?" Special case -- optional XML declaration */
					stat = STAT_DECLAR;
					break;
				}

				if (!is_valid_name_char(c, 1)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in start tag name: '%c'(%02x)\n", line, pos, c, c);
					goto err;
				}

				sbuf_add(&sbuf, c);

				stat = STAT_STAG_NAME_TAIL;
				break;

			case STAT_STAG_NAME_TAIL: /* Read the rest chars of the start tag name */
				if (is_space(c) || c == '/' || c == '>') {
					s = sbuf_dup(&sbuf);
					if (!s)
						goto err;
					/*
					 * We have to connect the created elem to the tree
					 * as soon as possible, since in any error it will be
					 * freed together with all other elems in the tree.
					 */
					elem = add_elem(s, XML_ELEM_TYPE_UNDEF,
							parent, &prev);
					if (!elem)
						goto err;

					if (!parent)
						root = elem;
#ifdef _XML_DBG
					printf("\nGet start tag name: %s\n", elem->name);
#endif

					if (is_space(c)) {
						stat = STAT_ATTR_NAME;
						break;
					}

					if (c == '/') {
						stat = STAT_EETAG;
						break;
					}

					if (c == '>') {
stag_close:
						elem->type = XML_ELEM_TYPE_ELEM;

						if (stack_push(&pch_stack,
								prev)) {
							ebuf_add(ebuf, "xml: too small internal pch stack\n");
							goto err;
						}

						parent = elem;
						prev_attr = NULL;
						prev = NULL;

						if (stack_push(&st_stack, elem->name)) {
							ebuf_add(ebuf, "xml: too small internal tag-match stack\n");
							goto err;
						}

						stat = STAT_TAG_OR_TEXT;
						break;
					}

					/* Must not be reachable */
					break;
				}

				if (!is_valid_name_char(c, 0)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in start tag name\n", line, pos);
					goto err;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: %d:%d: Too long start tag name\n", line, pos);
					goto err;
				}

				break;

			case STAT_EETAG: /* Empty-element tag: expect '>' after '/' */
				if (c != '>') {
					ebuf_add(ebuf, "xml: %d:%d: Empty-element tag: expected '>'\n", line, pos);
					goto err;
				}

#ifdef _XML_DBG
				printf("\nEmpty-element tag\n");
#endif
				elem->type = XML_ELEM_TYPE_EMPTY;

				prev_attr = NULL;

				stat = STAT_TAG_OR_TEXT;
				break;

			case STAT_TAG_OR_TEXT: /* Expect a start tag, end tag or text */
#ifdef _XML_DBG
				sleep(1);
#endif
				if (is_space(c))
					break; /* Skip */

				if (c != '<') { /* Text */
					if (c == '&') {
						p = esc;
						stat = STAT_TEXT_ESC;
					} else {
						sbuf_add(&sbuf, c);
						stat = STAT_TEXT_TAIL;
					}
					break;
				}

				stat = STAT_TAG;
				break;

			case STAT_TEXT_ESC: /* Escape sequence like &lt; */
				if (c == ';') {
					*p = '\0';
					c = escape_seq2char(esc);
					if (c < 0) {
						c = ';';
					} else {
#ifdef _XML_DBG
						printf("Found escape sequence: \"%s\" ('%c')\n", s, c);
#endif
					}

					if (sbuf_add(&sbuf, c)) {
						ebuf_add(ebuf, "xml: %d:%d: Too long text\n", line, pos);
						goto err;
					}

					stat = STAT_TEXT_TAIL;
					break;
				}

				if (!is_valid_esc_seq_char(c)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the escape sequence: '%c'\n", c);
					goto err;
				}

				if (p >= esc + sizeof(esc) - 1) {
					ebuf_add(ebuf, "xml: %d:%d: Too long escape sequence\n", line, pos);
					goto err;
				}
				*p++ = c;

				break;

			case STAT_TEXT_TAIL: /* Reading text tail */
				if (c == '<') {
					s = sbuf_dup(&sbuf);
					if (!s)
						goto err;

					elem = add_elem(s, XML_ELEM_TYPE_TEXT,
							parent, &prev);
					if (!elem)
						goto err;

#ifdef _XML_DBG
					printf("\nGet text: \"%s\"\n", elem->name);
#endif
					stat = STAT_TAG;
					break;
				}

				if (c == '&') {
					p = esc;
					stat = STAT_TEXT_ESC;
					break;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: %d:%d: Too long text\n", line, pos);
					goto err;
				}

				break;

			case STAT_TAG: /* Previous char is '<'  */
				if (c == '/') {
					stat = STAT_ETAG_NAME; /* Next must be closing tag name */
					break;
				}

				if (!is_valid_name_char(c, 1)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the start tag name\n", line, pos);
					goto err;
				}

				sbuf_add(&sbuf, c);

				stat = STAT_STAG_NAME_TAIL;
				break;

			case STAT_ATTR_NAME:
				if (is_space(c))
					break; /* Skip spaces */

				if (c == '/') {
					stat = STAT_EETAG;
					break;
				}

				if (c == '>')
					goto stag_close;

				if (!is_valid_name_char(c, 1)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the attr name: '%c'\n", line, pos, c);
					goto err;
				}

				sbuf_add(&sbuf, c);

				stat = STAT_ATTR_NAME_TAIL;
				break;

			case STAT_ATTR_NAME_TAIL: /* Read the rest of attribute name till space or equal sign */
				if (is_space(c)) {
					stat = STAT_ATTR_EQU;
					break;
				}

				if (c == '=') {
attr_name_equ:
					s = sbuf_dup(&sbuf);
					if (!s)
						goto err;

					attr = calloc(sizeof(*attr), 1);
					if (!attr) {
						fprintf(stderr, "%s:%d: No memory!\n", __FILE__, __LINE__);
						goto err;
					}

					attr->name = s;

					if (prev_attr) {
						prev_attr->pnext = attr;
					} else {
						elem->attr = attr;
					}

					prev_attr = attr;

					//printf("Get attr: %s\n", attr->name);

					stat = STAT_ATTR_VAL;
					break;
				}

				if (!is_valid_name_char(c, 0)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the attr name: '%c'\n", line, pos, c);
					goto err;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: %d:%d: Too long attr name\n", line, pos);
					goto err;
				}

				break;

			case STAT_ATTR_EQU: /* Skip spaces and wait for equal sign */
				if (is_space(c))
					break;

				if (c == '=')
					goto attr_name_equ;

				ebuf_add(ebuf, "xml: %d:%d: Expected spaces or equal sign after attr name. Got '%c'\n", line, pos, c);
				goto err;

			case STAT_ATTR_VAL: /* Wait for start double quote */
				if (is_space(c))
					break;

				if (c == '"') {
					stat = STAT_ATTR_VAL_TAIL;
					break;
				}

				ebuf_add(ebuf, "xml: %d:%d: Expected spaces or double quote in attr value. Got '%c'\n", line, pos, c);
				goto err;

			case STAT_ATTR_VAL_TAIL: /* Wait for end double quote */
				if (c == '"') {
					attr->val = sbuf_dup(&sbuf);
					if (!attr->val)
						goto err;

					/*
					printf("Get attr val: %s\n", attr->val);
					sleep(1);
					*/

					stat = STAT_ATTR_NAME;
					break;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: %d:%d: Too long attr value\n", line, pos);
					goto err;
				}

				/* TODO: escape sequences in attribute values */

				break;

			case STAT_ETAG_NAME:
				if (!is_valid_name_char(c, 1)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the end tag name\n", line, pos);
					goto err;
				}

				sbuf_add(&sbuf, c);

				stat = STAT_ETAG_NAME_TAIL;
				break;

			case STAT_ETAG_NAME_TAIL:
				if (c == '>') {
					p = sbuf_tail(&sbuf);
					*p = '\0';

#ifdef _XML_DBG
					printf("\nClosing tag name: %s\n", sbuf_buf(&sbuf));
#endif

					s = stack_pop(&st_stack);
					if (s == EMPTY_STACK) {
						ebuf_add(ebuf, "xml: %d:%d: Closing tag while no opening tags\n", line, pos);
						goto err;
					}

#ifdef _XML_DBG
					printf("\nCorresponding opening tag name: %s\n", s);
#endif

					if (strcmp(s, sbuf_buf(&sbuf))) {
						ebuf_add(ebuf, "xml: %d:%d: Opening tag doesn't match closing tag: \"%s\" vs \"%s\"\n", line, pos, s, sbuf_buf(&sbuf));
						goto err;
					}

					if (parent) {
						parent = parent->parent;
						prev = (struct xml_elem *)stack_pop(&pch_stack);
					}

					sbuf_trash(&sbuf);

					stat = STAT_TAG_OR_TEXT;
					break;
				}

				if (!is_valid_name_char(c, 0)) {
					ebuf_add(ebuf, "xml: %d:%d: Invalid char in the end tag name: '%c'\n", line, pos, c);
					goto err;
				}

				if (sbuf_add(&sbuf, c)) {
					ebuf_add(ebuf, "xml: %d:%d: Too long closing tag name\n", line, pos);
					goto err;
				}

				break;

			default:
				ebuf_add(ebuf, "xml: Bug: Unknown internal state: %d\n", stat);
				goto err;
		}
	}

	if (ferror(fp)) {
		ebuf_add(ebuf, "xml: Failed to read file: %s\n", strerror(errno));
		goto err;
	}

	s = stack_pop(&st_stack);
	if (s != EMPTY_STACK) {
		ebuf_add(ebuf, "xml: Not all tags have being closed\n");
		goto err;
	}

	return root;

err:
	xml_free(root);
	return NULL;
}

static int _print_indent(int n, FILE *fp)
{
	if (fprintf(fp, "%*s", n, "") < 0)
		return -1;

	return 0;
}

static int _print_attrs(struct xml_elem *elem, FILE *fp)
{
	struct xml_attr *p;

	p = elem->attr;
	while (p) {
		if (fprintf(fp, " %s=\"%s\"", p->name, p->val) < 0)
			return -1;
		p = p->pnext;
	}

	return 0;
}

static int _xml_print(struct xml_elem *root, int level, FILE *fp)
{
	struct xml_elem *p;

	if (_print_indent(level * 2, fp))
		return -1;

	switch (root->type) {
		default:
		case XML_ELEM_TYPE_ELEM:
			if (fprintf(fp, "<%s", root->name) < 0)
				return -1;

			if (_print_attrs(root, fp))
				return -1;

			p = root->child;
			if (p && !p->pnext && p->type == XML_ELEM_TYPE_TEXT &&
				strlen(p->name) < 50) {
				fprintf(fp, ">%s</%s>\n", p->name, root->name);
				break;
			}

			if (fprintf(fp, ">\n") < 0)
				return -1;

			p = root->child;
			while (p) {
				if (_xml_print(p, level + 1, fp))
					return -1;
				p = p->pnext;
			}

			if (_print_indent(level * 2, fp))
				return -1;

			if (fprintf(fp, "</%s>\n", root->name) < 0)
				return -1;

			break;

		case XML_ELEM_TYPE_EMPTY:
			if (fprintf(fp, "<%s", root->name) < 0)
				return -1;

			if (_print_attrs(root, fp))
				return -1;

			if (fprintf(fp, "/>\n") < 0)
				return -1;
			break;

		case XML_ELEM_TYPE_TEXT:
			if (fprintf(fp, "%s\n", root->name) < 0)
				return -1;
			break;
	}

	return 0;
}

int xml_print(struct xml_elem *root, FILE *fp)
{
	return _xml_print(root, 0, fp);
}

void xml_free(struct xml_elem *root)
{
	struct xml_elem *p, *q;
	struct xml_attr *r, *s;

	if (!root)
		return;

	p = root->child;
	while (p) {
		q = p->pnext;
		xml_free(p);
		p = q;
	}

	r = root->attr;
	while (r) {
		s = r->pnext;
		free(r->name);
		free(r->val);
		free(r);
		r = s;
	}

	free(root->name);
	free(root);
}

/* Get direct child by name (return the first found) */
struct xml_elem *xml_get_child(struct xml_elem *elem, const char *name)
{
	struct xml_elem *p;

	p = elem->child;
	while (p) {
		if (!strcmp(p->name, name))
			return p;
		p = p->pnext;
	}

	return NULL;
}

/*
 * Get next name from path in @buf. Each name must be start with '/'.
 * Return NULL in case of error. Return pointer to the next name in path.
 */
static const char *path_next(const char *path, char *buf, int sz)
{
	char *p;

	if (*path++ != '/')
		return NULL;

	p = buf;
	while (*path != '/' && *path) {
		if (p - buf > sz - 1)
			return NULL;
		*p++ = *path++;
	}

	*p = '\0';

	return path;
}

/* Get element by path in tree (first found) */
struct xml_elem *xml_get_elem(struct xml_elem *root, const char *path)
{
	char name[256];
	const char *p;
	struct xml_elem *elem;

	p = path_next(path, name, sizeof(name));
	if (!p)
		return NULL;

	if (strcmp(root->name, name))
		return NULL;

	elem = root;
	do {
		p = path_next(p, name, sizeof(name));
		elem = xml_get_child(elem, name);
	} while (elem && *p);

	return elem;
}

/* Get elem attribute by name and return value */
char *xml_get_attr(struct xml_elem *elem, const char *name)
{
	struct xml_attr *p;

	p = elem->attr;
	while (p) {
		if (!strcmp(p->name, name))
			return p->val;
		p = p->pnext;
	}

	return NULL;
}

/* Get child by name and attribute value */
struct xml_elem *xml_get_child_with_attr(struct xml_elem *elem,
					 const char *name,
					 const char *attr,
					 const char *val)
{
	struct xml_elem *p;
	char *v;

	p = elem->child;
	while (p) {
		if (!strcmp(p->name, name)) {
			v = xml_get_attr(p, attr);
			if (v && !strcmp(v, val))
				return p;
		}
		p = p->pnext;
	}

	return NULL;
}
