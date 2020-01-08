#include <benchmark/benchmark.hpp>

#include <executors/executors.h>

#include <thread>
#include <mutex>
#include <condition_variable>


class EmptyTask : public Task {
public:
    virtual void run() override {}
};

static void BM_simple_submit(benchmark::State& state) {
    auto executor = MakeThreadPoolExecutor(state.range(0));
    for (auto _ : state) {
        auto task = std::make_shared<EmptyTask>();
        executor->submit(task);
        task->wait();
    }
}

BENCHMARK(BM_simple_submit)
    ->Arg(1)->Arg(2)->Arg(4);

static void BM_fanout_fanin(benchmark::State& state) {
    auto executor = MakeThreadPoolExecutor(state.range(0));
    for (auto _ : state) {
        auto firstTask = std::make_shared<EmptyTask>();
        auto lastTask = std::make_shared<EmptyTask>();

        for (int i = 0; i < state.range(1); i++) {
            auto middleTask = std::make_shared<EmptyTask>();
            middleTask->addDependency(firstTask);
            lastTask->addDependency(middleTask);

            executor->submit(middleTask);
        }

        executor->submit(firstTask);
        executor->submit(lastTask);

        lastTask->wait();
    }
}

BENCHMARK(BM_fanout_fanin)
    ->Args({1, 1})
    ->Args({1, 10})
    ->Args({1, 100})
    ->Args({2, 1})
    ->Args({2, 10})
    ->Args({2, 100})
    ->Args({10, 1})
    ->Args({10, 10})
    ->Args({10, 100});

class Latch {
public:
    Latch(size_t count) : counter_(count) {}

    void wait() {
        std::unique_lock<std::mutex> guard(lock_);
        done_.wait(guard, [this] { return counter_ == 0; });
    }

    void signal() {
        std::unique_lock<std::mutex> guard(lock_);
        counter_--;
        if (counter_ == 0) {
            done_.notify_all();
        }
    }

private:
    std::mutex lock_;
    std::condition_variable done_;
    size_t counter_;
};

class LatchSignaler : public Task {
public:
    LatchSignaler(Latch* latch) : latch_(latch) {}

    virtual void run() override {
        latch_->signal();
    }
private:
    Latch* latch_;
};

static void BM_scalable_timers(benchmark::State& state) {
    auto executor = MakeThreadPoolExecutor(state.range(0));

    for (auto _ : state) {
        Latch latch(state.range(1));
        auto at = std::chrono::system_clock::now() + std::chrono::milliseconds(5);

        for (size_t i = 0; i < static_cast<size_t>(state.range(1)); i++) {
            auto task = std::make_shared<LatchSignaler>(&latch);

            task->setTimeTrigger(at + std::chrono::microseconds((i * i) % 1000));
            executor->submit(task);
        }

        latch.wait();
    }
}

BENCHMARK(BM_scalable_timers)
    ->Args({1, 100000})
    ->Args({2, 100000})
    ->Args({5, 100000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
