#include <memory>
#include <chrono>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <atomic> 
#include <vector>
#include <thread>
#include <condition_variable>
#include <exception>


class Task : public std::enable_shared_from_this<Task> {
public:
    friend class CustomExecutor;
    virtual ~Task() {}

    virtual void run() = 0;
    
    Task();
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
    bool isReady();
    void _setExp(std::exception_ptr exp);
    void resolveDependencies();
    void resolveTriggers();
    void boom();

    enum States {Init, Completed, Failed, Canceled};
    std::atomic<States> _state;
    std::atomic_bool _is_finised;
    std::exception_ptr _exp;
    std::condition_variable finish;
    mutable std::mutex m;

    // WARNING, not obvious: vector of task, that depend from this task
    std::vector<Task*> to_dependencies;
    //std::vector<std::shared_ptr<Task>> to_dependencies;

    // count tasks that must be finished before this
    std::atomic_int count_of_dependencies;

    std::atomic_bool trigger_done;
    std::vector<Task*> to_triggered;

    std::chrono::system_clock::time_point deadline;
};

template<class T>
class Future;

template<class T>
using FuturePtr = std::shared_ptr<Future<T>>;

// Used instead of void in generic code
struct Unit {};

class CustomExecutor;

class Executor {
public:
    virtual ~Executor() {}

    virtual void submit(std::shared_ptr<Task> task) = 0;

    virtual void startShutdown() = 0;
    virtual void waitShutdown() = 0;
    
    template<class T>
    FuturePtr<T> invoke(std::function<T()> fn) {
        FuturePtr<T> tmp = std::make_shared<Future<T>>(fn);
        submit(tmp);
        return tmp;
    }
    
    template<class Y, class T>
    FuturePtr<Y> then(FuturePtr<T> input, std::function<Y()> fn) {
        FuturePtr<Y> tmp = std::make_shared<Future<Y>>(fn);
        if(!input->isFinished())
            tmp->addDependency(input);
        submit(tmp);
        return tmp;
    }

    template<class T>
    FuturePtr<std::vector<T>> whenAll(std::vector<FuturePtr<T>> all) {
        FuturePtr<std::vector<T>> tmp = std::make_shared<Future<std::vector<T>>>([all]{
            std::vector<T> res;
            for(auto &t: all) {
                res.push_back(t->get());
            }
            return res;
        });
        for(auto t: all) {
            if(!t->isFinished())
                tmp->addDependency(t);
        }
        submit(tmp);
        return tmp;
    }

    template<class T>
    FuturePtr<T> whenFirst(std::vector<FuturePtr<T>> all) {
        FuturePtr<T> tmp = std::make_shared<Future<T>>([all]{
            for(auto t: all)
                if(t->isFinished())
                    return t->get();
        });

        for(auto t: all) {
            if(t->isFinished())
                break;
            tmp->addTrigger(t);
        }
        submit(tmp);
        return tmp;
    }

    template<class T>
    FuturePtr<std::vector<T>> whenAllBeforeDeadline(std::vector<FuturePtr<T>> all,
                                                    std::chrono::system_clock::time_point deadline) {
        FuturePtr<std::vector<T>> tmp = std::make_shared<Future<std::vector<T>>>([all, deadline]{
            std::mutex lm;
            std::unique_lock<std::mutex> lk(lm);
            std::condition_variable cv;
            std::vector<T> res;
            if (cv.wait_until(lk, deadline) == std::cv_status::timeout)
            {
                for(auto t: all)
                    if(t->isFinished())
                        res.push_back(t->get());
            }
            return res;
        });
        submit(tmp);
        return tmp;
    }
                                                    
};

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads);

template<class T>
class Future : public Task {
public:
    Future(std::function<T()> e): exe(e) {}

    T get() {
        wait();
        if(getError()) std::rethrow_exception(getError());
        return result;
    }
    
private:
    void run() override {
        result = exe();
    }
    std::function<T()> exe;
    T result;
};


template<class T>
class ThreadSafeQueue {
 public:
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(m);
        if(data.empty()) return false;
        
        value = std::move(data.front());
        data.pop();
        return true;
    }
    //template <typename U>
    void push(T value) {
        std::lock_guard<std::mutex> lock(m);
        data.push(std::move(value));
    }

 private:
    std::queue<T> data;
    mutable std::mutex m;
};

class CustomExecutor : public Executor {
public:
    CustomExecutor(int num): done(false) {
        for (int i = 0; i < num; i++)
        {
            threads.push_back(std::thread(&CustomExecutor::worker_thread, this));
        }
        
    }

    ~CustomExecutor() override {
        done = true;
        waitShutdown();
    }

    void submit(std::shared_ptr<Task> task) override {
        if(done) task->cancel();
        _q.push(task);
    }

    void startShutdown() override {
        done = true;
    }
    void waitShutdown() override {
        for(auto& t: threads) {
            if(t.joinable()) {
                t.join();
            }
        }
    }
private:
    std::atomic_bool done;
    ThreadSafeQueue<std::shared_ptr<Task>> _q;
    std::vector<std::thread> threads;

    void worker_thread() {
        while (!done)
        {
            std::shared_ptr<Task> task;
            if(_q.try_pop(task)) {

                if(task->isCanceled()) {
                    task->boom();
                    continue;
                }

                if(task->isReady()) {
                    try
                    {
                        task->run();
                        task->_state = Task::States::Completed;
                        
                    }
                    catch(...)
                    {
                        task->_setExp(std::current_exception());
                        task->_state = Task::States::Failed;
                    }
                }
                else {
                    _q.push(task);
                    continue;
                }
                task->boom();
            }
            else {
                std::this_thread::yield();
            }
            
        }
        
    }

};

/*
template<class T>
class Future : public Task {
public:
    T get();

private:
};*/

/*
template<class T>
class ThreadSafeQueue {
 public:
    ThreadSafeQueue() : head(new node), tail(head.get()){}

    std::shared_ptr<T> try_pop() {
        std::unique_ptr<node> old_head = try_pop_head();
        return old_head ? old_head->data : std::shared_ptr<T>();
    }
    bool try_pop(T& value) {
        std::unique_ptr<node> const old_head = try_pop_head(value);
        return old_head == nullptr ? false : true;
    }
    std::shared_ptr<T> wait_and_pop() {
        std::unique_ptr<node> const old_head = wait_pop_head();
        return old_head->data;
    }
    void wait_and_pop(T& value) {
        std::unique_ptr<node> const old_head = wait_pop_head(value);
    }
    void push(T new_value) {
        //std::shared_ptr<T new_data(std::make_shared<T>(std::move(new_value)));
        std::unique_ptr<node> p (new node);

        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        tail->data = new_value;
        node* const new_tail = p.get();
        tail->next = std::move(p);
        tail = new_tail;

        data_cond.notify_one();
    }
    bool empty() {
        std::lock_guard<std::mutex> head_locl(head_mutex);
        return (head.get() == get_tail());
    }
 private:
    struct node
    {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };
    std::mutex head_mutex;
    std::unique_ptr<node> head;
    std::mutex tail_mutex;
    node* tail;
    std::condition_variable data_cond;




    std::unique_ptr<node> try_pop_head() {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get() == get_tail()) {
            return std::unique_ptr<node>();
        }
        return pop_head();
    }

    std::unique_ptr<node> try_pop_head(T value) {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get() == get_tail()) {
            return std::unique_ptr<node>();
        }
        value = std::move(*head->data);
        return pop_head();
    }

    node* get_tail() {
        std::lock_guard<std::mutex> tail_lock(tail_mutex);
        return tail;
    }

    std::unique_ptr<node> pop_head() {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get() == get_tail()) {
            return nullptr;
        }
        std::unique_ptr<node> old_head = std::move(head);
        head = std::move(old_head->next);
        return old_head;
    }

    std::unique_lock<std::mutex> wait_for_data() {
        std::unique_lock<std::mutex> head_lock (head_mutex);
        data_cond.wait(head_lock, [&]{return head.get() != get_tail();});
        return std::move(head_lock);
    }

    std::unique_ptr<node> waip_pop_head() {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        return pop_head();
    }

    std::unique_ptr<node> wait_pop_head(T& value) {
        std::unique_lock<std::mutex> head_lock(wait_for_data());
        value = std::move((*head)->data);
        return pop_head();
    }
};
*/