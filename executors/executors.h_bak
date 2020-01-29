#include <memory>
#include <chrono>
#include <vector>
#include <functional>

class Task : public std::enable_shared_from_this<Task> {
public:
    virtual ~Task() {}

    virtual void run() = 0;

    void addDependency(std::shared_ptr<Task> dep);
    void addTrigger(std::shared_ptr<Task> dep);
    void setTimeTrigger(std::chrono::system_clock::time_point at);
    
    // Task::run() completed without throwing exception
    bool isCompleted();

    // Task::run() throwed exception
    bool isFailed();

    // Task was canceled
    bool isCanceled();

    // Task either completed, failed or was canceled
    bool isFinished();
    
    std::exception_ptr getError();

    void cancel();

    void wait();

private:
};

template<class T>
class Future;

template<class T>
using FuturePtr = std::shared_ptr<Future<T>>;

// Used instead of void in generic code
struct Unit {};

class Executor {
public:
    virtual ~Executor() {}

    virtual void submit(std::shared_ptr<Task> task) = 0;

    virtual void startShutdown() = 0;
    virtual void waitShutdown() = 0;

    template<class T>
    FuturePtr<T> invoke(std::function<T()> fn);

    template<class Y, class T>
    FuturePtr<Y> then(FuturePtr<T> input, std::function<Y()> fn);

    template<class T>
    FuturePtr<std::vector<T>> whenAll(std::vector<FuturePtr<T>> all);

    template<class T>
    FuturePtr<T> whenFirst(std::vector<FuturePtr<T>> all);

    template<class T>
    FuturePtr<std::vector<T>> whenAllBeforeDeadline(std::vector<FuturePtr<T>> all,
                                                    std::chrono::system_clock::time_point deadline);
};

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads);

template<class T>
class Future : public Task {
public:
    T get();

private:
};
