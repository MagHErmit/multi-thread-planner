#include <gtest/gtest.hpp>

#include <thread>
#include <chrono>
#include <atomic>

#include <executors/executors.h>

struct FutureTest : public ::testing::Test {
    std::shared_ptr<Executor> pool;

    FutureTest() {
        pool = MakeThreadPoolExecutor(2);
    }
};

TEST_F(FutureTest, InvokeVoid) {
    int x = 0;
    auto future = pool->invoke<Unit>([&] () -> Unit {
        x = 42;

        return Unit{};
    });

    future->get();
    ASSERT_EQ(x, 42);
}

TEST_F(FutureTest, InvokeString) {
    auto future = pool->invoke<std::string>([] () {
        return "Hello World";
    });

    ASSERT_EQ(future->get(), std::string("Hello World"));
}

TEST_F(FutureTest, InvokeException) {
    auto future = pool->invoke<Unit>([] () -> Unit {
        throw std::logic_error("Test");
    });

    ASSERT_THROW(future->get(), std::logic_error);
}

TEST_F(FutureTest, DISABLED_Then) {
    auto future_a = pool->invoke<std::string>([] () {
        return std::string("Foo Bar");
    });

    auto future_b = pool->then<Unit>(future_a, [future_a] () {
        EXPECT_TRUE(future_a->isFinished());
        EXPECT_EQ(future_a->get(), std::string("Foo Bar"));

        return Unit{};
    });

    future_b->get();
    EXPECT_TRUE(future_b->isFinished());
}

TEST_F(FutureTest, ThenIsNonBlocking) {
    auto start = std::chrono::system_clock::now();

    auto future_a = pool->invoke<std::string>([] () {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return std::string("Foo Bar");
    });

    auto future_b = pool->then<Unit>(future_a, [future_a] () {
        return Unit{};
    });

    auto delta = std::chrono::system_clock::now() - start;
    EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count(), 50);
}

TEST_F(FutureTest, WhenAll) {
    const size_t N = 100;
    std::atomic<size_t> count{0};

    std::vector<FuturePtr<size_t>> all;
    for (size_t i = 0; i < N; i++) {
        all.emplace_back(pool->invoke<size_t>([&, i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            count++;
            return i;
        }));
    }

    auto results = pool->whenAll(all)->get();
    ASSERT_EQ(N, count.load());
    ASSERT_EQ(N, results.size());
    for (size_t i = 0; i < N; i++) {
        EXPECT_EQ(results[i], i);
    }
}

TEST_F(FutureTest, DISABLED_WhenFirst) {
    auto start = std::chrono::system_clock::now();
    auto firstFuture = pool->invoke<int>([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 1;
    });

    auto lastFuture = pool->invoke<int>([] {
        return 2;
    });

    auto result = pool->whenFirst(std::vector<FuturePtr<int>>{firstFuture, lastFuture})->get();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start);

    ASSERT_EQ(2, result);
    ASSERT_LE(time.count(), 50);
}

TEST_F(FutureTest, DISABLED_WhenAllBeforeDeadline) {
    const size_t N = 10;
    auto start = std::chrono::system_clock::now();

    std::vector<FuturePtr<int>> all;
    for (size_t i = 0; i < N; i++) {
        all.push_back(pool->invoke<int>([&, i] {
            return i;
        }));
    }

    auto slowFuture = pool->invoke<Unit>([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return Unit{};
    });
    
    for (size_t i = 0; i < N; i++) {
        all.push_back(pool->then<int>(slowFuture, [&, i] {
            return N + i;
        }));
    }

    auto result = pool->whenAllBeforeDeadline(all, start + std::chrono::milliseconds(50))->get();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start);

    ASSERT_EQ(result.size(), N);
    ASSERT_LE(time.count(), 80);
}
