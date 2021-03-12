#ifndef _STACK_H
#define _STACK_H

struct stack {
	void **bottom;
	void **top;
	void **max;
};

void stack_init(struct stack *stack, void **buf, int sz);

int stack_push(struct stack *stack, void *p);

#define EMPTY_STACK ((void *)-1)

void *stack_pop(struct stack *stack);

#endif

