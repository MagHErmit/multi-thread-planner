#include <executors/executors.h>


#include <iostream>



    Task::Task(): _state(States::Init), _is_finised(false), trigger_done(true), deadline(std::chrono::system_clock::now()) {}
    
    void Task::addDependency(std::shared_ptr<Task> dep) {
        dep->to_dependencies.push_back(this);
        count_of_dependencies++;
    }
    
    /*void Task::addDependency(std::shared_ptr<Task> dep) {
        to_dependencies.push_back(dep);
        //count_of_dependencies++;
    }*/
    
    void Task::addTrigger(std::shared_ptr<Task> dep) {
        dep->to_triggered.push_back(this);
        trigger_done = false;
    }
    
    void Task::setTimeTrigger(std::chrono::system_clock::time_point at) {
        deadline = at;
    }
    
    // Task::run() completed without throwing exception
    bool Task::isCompleted() {
        return _state == States::Completed;
    }
    
    // Task::run() throwed exception
    bool Task::isFailed() {
        return _state == States::Failed;
    }

    // Task was canceled
    bool Task::isCanceled() {
        return _state == States::Canceled;
    }

    // Task either completed, failed or was canceled
    bool Task::isFinished() {
        return _is_finised;
    }
    
    std::exception_ptr Task::getError() {
        return _exp;
    }

    void Task::cancel() {
        _state = States::Canceled;
        boom();
    }

    void Task::wait() {
        // std::unique_lock<std::mutex> lock(m);
        while(!isFinished()) {
            // finish.wait(lock);
        }
    }

    void Task::_setExp(std::exception_ptr exp) {
        _exp = exp;
    }

    bool Task::isReady() {
        if(count_of_dependencies > 0) return false;
        // for(auto &t : to_dependencies)
        //     if(!t->isReady()) return false;

        if(!trigger_done) return false; 
        if(deadline > std::chrono::system_clock::now()) return false;
        return true;
    }

    void Task::resolveDependencies(){
        for(auto &t: to_dependencies) {
            t->count_of_dependencies--;
        }
    }

    void Task::resolveTriggers() {
        for(auto &t: to_triggered) {
            t->trigger_done = true;
        }
    }

    void Task::boom() {
        resolveDependencies();
        resolveTriggers();
        _is_finised = true;
        finish.notify_all();
    }

std::shared_ptr<Executor> MakeThreadPoolExecutor(int num_threads) {
    return std::make_shared<CustomExecutor>(num_threads);
}

    