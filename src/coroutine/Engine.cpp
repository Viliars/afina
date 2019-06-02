#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
	char stack_now;
	//TODO Поддержка разных стеков
	char *top = &stack_now;
	ctx.Low = top;
	ctx.High = StackBottom;
	size_t size = ctx.High - ctx.Low;
	// Смотрим хватает ли нам места для сохранения
	// Если нет, то выделяем больше памяти
	if (size > std::get<1>(ctx.Stack)) {
		delete std::get<0>(ctx.Stack);
		std::get<0>(ctx.Stack) = new char[size];
		std::get<1>(ctx.Stack) = size;
	}
	// Сохраняем все
	memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
	char stack_now;
	// Раскручиваем стек
	if (&stack_now >= ctx.Low) {
		Restore(ctx);
	}
	// Восстанавливаем контекст
	memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
	longjmp(ctx.Environment, 1);
}

void Engine::yield() {
	context *next = alive;
	if (next == cur_routine && next != nullptr) {
		next = next->next;
	}

	if (next == nullptr) {
		return;
	}

	sched(next);
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
