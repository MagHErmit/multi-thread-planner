#include <gtest/gtest.hpp>

#include <thread>
#include <chrono>

#include <executors/executors.h>

typedef std::function<std::shared_ptr<Executor>()>
        ExecutorMaker;

struct ExecutorsTest : public testing::TestWithParam<ExecutorMaker> {
    std::shared_ptr<Executor> pool;

    ExecutorsTest() {
        pool = GetParam()();
    }
};

TEST_P(ExecutorsTest, Destructor) {
}

TEST_P(ExecutorsTest, Shutdown) {
    pool->startShutdown();
    pool->waitShutdown();
}

class TestTask : public Task {
public:
    bool completed = false;

    void run() override {
        EXPECT_TRUE(!completed) << "Seems like task body was run multiple times";
        completed = true;
    }
};

class FailingTestTask : public Task {
public:
    void run() override {
        throw std::logic_error("Failed");
    }
};

TEST_P(ExecutorsTest, RunSingleTask) {
    auto task = std::make_shared<TestTask>();

    pool->submit(task);

    task->wait();

    EXPECT_TRUE(task->completed);
    EXPECT_TRUE(task->isFinished());
    EXPECT_FALSE(task->isCanceled());
    EXPECT_FALSE(task->isFailed());
}

TEST_P(ExecutorsTest, RunSingleFailingTask) {
    auto task = std::make_shared<FailingTestTask>();

    pool->submit(task);

    task->wait();

    EXPECT_FALSE(task->isCompleted());
    EXPECT_FALSE(task->isCanceled());
    EXPECT_TRUE(task->isFailed());

    EXPECT_THROW(std::rethrow_exception(task->getError()), std::logic_error);
}

TEST_P(ExecutorsTest, CancelSingleTask) {
    auto task = std::make_shared<TestTask>();
    task->cancel();
    task->wait();

    EXPECT_FALSE(task->isCompleted());
    EXPECT_TRUE(task->isCanceled());
    EXPECT_FALSE(task->isFailed());
    
    pool->submit(task);
    task->wait();

    EXPECT_FALSE(task->isCompleted());
    EXPECT_TRUE(task->isCanceled());
    EXPECT_FALSE(task->isFailed());
}

TEST_P(ExecutorsTest, TaskWithSingleDependency) {
    auto task = std::make_shared<TestTask>();
    auto dependency = std::make_shared<TestTask>();

    task->addDependency(dependency);

    pool->submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());

    pool->submit(dependency);

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, TaskWithSingleCompletedDependency) {
    auto task = std::make_shared<TestTask>();
    auto dependency = std::make_shared<TestTask>();

    task->addDependency(dependency);

    pool->submit(dependency);
    dependency->wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_FALSE(task->isFinished());

    pool->submit(task);
    task->wait();
    ASSERT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, FailedDependencyIsConsideredCompleted) {
    auto task = std::make_shared<TestTask>();
    auto dependency = std::make_shared<FailingTestTask>();

    task->addDependency(dependency);

    pool->submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());

    pool->submit(dependency);

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, CanceledDependencyIsConsideredCompleted) {
    auto task = std::make_shared<TestTask>();
    auto dependency = std::make_shared<TestTask>();

    task->addDependency(dependency);

    pool->submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());

    dependency->cancel();

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

struct RecursiveTask : public Task {
    RecursiveTask(int _n, std::shared_ptr<Executor> _executor) : n(_n), executor(_executor) {}

    void run() {
        if (n > 0) {
            executor->submit(std::make_shared<RecursiveTask>(n - 1, executor));
        }

        if (n == 0) {
            executor->startShutdown();
        }
    }
    
    const int n;
    const std::shared_ptr<Executor> executor;
};

TEST_P(ExecutorsTest, RunRecursiveTask) {
    auto task = std::make_shared<RecursiveTask>(100, pool);
    pool->submit(task);

    pool->waitShutdown();
}

TEST_P(ExecutorsTest, TaskWithSingleTrigger) {
    auto task = std::make_shared<TestTask>();
    auto trigger = std::make_shared<TestTask>();

    task->addTrigger(trigger);
    pool->submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());
    
    pool->submit(trigger);

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, TaskWithSingleCompletedTrigger) {
    auto task = std::make_shared<TestTask>();
    auto trigger = std::make_shared<TestTask>();

    task->addTrigger(trigger);
    pool->submit(trigger);
    trigger->wait();

    EXPECT_FALSE(task->isFinished());
    
    pool->submit(task);

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, TaskWithTwoTrigger) {
    auto task = std::make_shared<TestTask>();
    auto trigger_a = std::make_shared<TestTask>();
    auto trigger_b = std::make_shared<TestTask>();

    task->addTrigger(trigger_a);
    task->addTrigger(trigger_b);
    
    pool->submit(task);
    pool->submit(trigger_b);
    pool->submit(trigger_a);

    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, MultipleDependencies) {
    auto task = std::make_shared<TestTask>();
    auto dep1 = std::make_shared<TestTask>();
    auto dep2 = std::make_shared<TestTask>();

    task->addDependency(dep1);
    task->addDependency(dep2);

    pool->submit(task);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());

    pool->submit(dep1);
    dep1->wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(task->isFinished());

    pool->submit(dep2);
    task->wait();

    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, TaskWithSingleTimeTrigger) {
    auto task = std::make_shared<TestTask>();

    task->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(200));

    pool->submit(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(task->isFinished());
    
    task->wait();
    EXPECT_TRUE(task->isFinished());
}

TEST_P(ExecutorsTest, DISABLED_TaskTriggeredByTimeAndDep) {
    auto task = std::make_shared<TestTask>();
    auto dep = std::make_shared<TestTask>();

    task->addDependency(dep);
    task->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(2));

    pool->submit(task);
    pool->submit(dep);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(task->isFinished());

    std::this_thread::sleep_for(std::chrono::milliseconds(3));
}

TEST_P(ExecutorsTest, MultipleTimerTriggers) {
    auto task_a = std::make_shared<TestTask>();
    auto task_b = std::make_shared<TestTask>();

    task_a->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(50));
    task_b->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(1));

    pool->submit(task_a);
    pool->submit(task_b);

    task_a->wait();
    EXPECT_TRUE(task_b->isFinished());
}

TEST_P(ExecutorsTest, MultipleTimerTriggersWithReverseOrder) {
    auto task_a = std::make_shared<TestTask>();
    auto task_b = std::make_shared<TestTask>();

    task_a->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(50));
    task_b->setTimeTrigger(std::chrono::system_clock::now() + std::chrono::milliseconds(1));

    pool->submit(task_b);
    pool->submit(task_a);

    task_b->wait();
    EXPECT_FALSE(task_a->isFinished());
}

TEST_P(ExecutorsTest, PossibleToCancelAfterSubmit) {
    std::vector<std::shared_ptr<TestTask>> tasks;
    for (int i = 0; i < 1000; ++i) {
        auto task = std::make_shared<TestTask>();
        tasks.push_back(task);

        pool->submit(task);

        task->cancel();
    }

    pool.reset();

    for (auto t : tasks) {
        if (!t->completed) return;
    }

    FAIL() << "Seems like cancel() doesn't affect submitted tasks";
}

struct RecursiveGrowingTask : public Task {
    RecursiveGrowingTask(int _n, int _fanout,
                         std::shared_ptr<Executor> _executor)
        : n(_n), fanout(_fanout), executor(_executor) {}

    void run() {
        if (n > 0) {
            for (int i = 0; i < fanout; ++i) {
                executor->submit(std::make_shared<RecursiveGrowingTask>(n - 1, fanout, executor));
            }
        }

        if (n == 0) {
            executor->startShutdown();
        }
    }
    
    const int n, fanout;
    const std::shared_ptr<Executor> executor;
};

TEST_P(ExecutorsTest, NoDeadlockWhenSubmittingFromTaskBody) {
    auto task = std::make_shared<RecursiveGrowingTask>(5, 10, pool);
    pool->submit(task);

    pool->waitShutdown();
}

INSTANTIATE_TEST_CASE_P(ThreadPool, ExecutorsTest,
                        ::testing::Values(
                            [] { return MakeThreadPoolExecutor(1); },
                            [] { return MakeThreadPoolExecutor(2); },
                            [] { return MakeThreadPoolExecutor(10); }
                        ));
