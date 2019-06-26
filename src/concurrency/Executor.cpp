#include <afina/concurrency/Executor.h>
#include <iostream>

namespace Afina {
namespace Concurrency {

Executor::Executor(uint low_watermark, uint high_watermark, uint max_queue_size, uint idle_time):
    _low_watermark(low_watermark), _hight_watermark(high_watermark),
    _max_queue_size(max_queue_size), _idle_time(idle_time) {
    // init State
    state = Executor::State::kRun;
    // Создаем  минимум тредов для
    // Берем лок для инициилизации
    std::unique_lock<std::mutex> lock(mutex);


    for (int i = 0; i < _low_watermark; ++i) {
        // поток начнет выполнение переданной функции perform
        std::thread thread = std::thread(perform, this);
        // отправляем поток в свободное плавание
        thread.detach();
    }

    // мы создали $_low_watermark тредов и они все готовы к работе
    _now_threads = _low_watermark;
    _ready_threads = _low_watermark;
};

Executor::~Executor() {
    Stop(true);
    // ВОПРОС Верну ли я думаю, что если поставить false
    // то могут быть ошибки, когда освободится память под тредпул
}


void perform(Executor *executor) {
    // возьмем лок для получения задачи из общей очереди
    std::unique_lock<std::mutex> lock(executor->mutex);

    // основной цикл потока
    // после каждой задачи проверяем, что тредпул не останавливают
    while (executor->state == Executor::State::kRun) {
        // не нарушаем _low_watermark инварианту
        if (executor->_now_threads > executor->_low_watermark) {
            std::chrono::milliseconds wait_time = std::chrono::milliseconds(executor->_idle_time);
            // wait_for или wait_until
            // выбрал wait_for - меньше операций
            bool result = executor->empty_condition.wait_for(lock, wait_time,
              [executor]() { return !(executor->tasks.empty()) || (executor->state != Executor::State::kRun);});
            // получаем false только в одной случае - время вышло, а условие не стало true
            if(!result) { break; }
        }
        // не нарушаем _low_watermark инварианту
        else {
            executor->empty_condition.wait(lock,
              [executor]() { return !(executor->tasks.empty()) || (executor->state != Executor::State::kRun);});
        }
        // лок все еще держим
        executor->_ready_threads -= 1;
        // берем задачу из очереди задач
        std::function<void()> task = (executor->tasks).back();
        executor->tasks.pop_back();
        // больше нам лок не нужен - отдаем, чтобы другие задачи могли взять задачи
        lock.unlock();
        // нельзя позволить задаче убить поток исключением
        try
        {
            (task)();
        }
        catch (...) {}

        // мы выполнили задачу, можно брать другую - берем лок
        lock.lock();
        executor->_ready_threads += 1;
    }

    executor->_now_threads -= 1;
    executor->_ready_threads -= 1;
    // если это последний поток, который останавливался
    // переводим в состояние Executor::State::kStopped
    if ((executor->state == Executor::State::kStopping) && (executor->_now_threads == 0)) {
        executor->state = Executor::State::kStopped;
        // будим главный поток, если он ждет
        executor->end_condition.notify_one();
    }
}

void Executor::Stop(bool await = false) {
    // берем лок
    std::unique_lock<std::mutex> lock(mutex);
    // говорим потокам, что мы останавливаемся
    state = Executor::State::kStopping;

    if (_now_threads == 0) {
        // можем остановить прям щас
        state = Executor::State::kStopped;
        return;
    }
    // будем все потоки, которые выписли на ожидании задачи
    empty_condition.notify_all();
    // виснем сами, пока все потоки не завершатся
    if (await) {
        end_condition.wait(lock, [this]() { return (this->state == Executor::State::kStopped); });
    }
}


} // namespace Concurrency
} // namespace Afina
