#include <stdio.h>

#include "stack.h"

void stack_init(struct stack *stack, void **buf, int sz)
{
	stack->bottom = stack->top = buf;
	stack->max = stack->bottom + sz;
}

int stack_push(struct stack *stack, void *p)
{
	if (stack->top >= stack->max)
		return -1; /* Full */

	*(stack->top)++ = p;

	return 0;
}

void *stack_pop(struct stack *stack)
{
	if (stack->top == stack->bottom)
		return EMPTY_STACK; /* Empty */

	return *--(stack->top);
}
