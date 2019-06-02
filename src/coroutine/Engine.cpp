#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
	char stack_pos;
	char *top = &stack_pos;

	ctx.Low = top; // assume stack grows down
	ctx.High = StackBottom;
	size_t size = ctx.High - ctx.Low;

	if (size > std::get<1>(ctx.Stack)) {
		delete std::get<0>(ctx.Stack);
		std::get<0>(ctx.Stack) = new char[size];
		std::get<1>(ctx.Stack) = size;
	}

	memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
	char stack_pos;
	if (&stack_pos >= ctx.Low) {
		Restore(ctx); // so why not use array?
	}

	memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
	longjmp(ctx.Environment, 1);
}

void Engine::yield() {
	context *todo = alive;
	if (todo == cur_routine && todo != nullptr) {
		todo = todo->next;
	}

	if (todo == nullptr) {
		return;
	}

	sched(todo);
}

void Engine::sched(void *routine_) {
	context *ctx = (context*) routine_;

	if (cur_routine != nullptr) {
		if (setjmp(cur_routine->Environment) > 0) {
			return;
		}
		Store(*cur_routine);
	}

	cur_routine = ctx;
	Restore(*cur_routine);
}

} // namespace Coroutine
} // namespace Afina
