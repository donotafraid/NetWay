#include "Thread_pool/Thread_pool_define.h"


Thread_pool::~Thread_pool()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        is_active_flag = true ;
        tasks = std::queue<std::function<void()>>();
    }
    cv_notify.notify_all();

    for ( auto &t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    std::cout<<"Thread_pool destructor called!"<<std::endl;
}

void Thread_pool::start()
{
    for (size_t i = 0 ; i < THREAD_NUM ; i++)
    {
        threads.emplace_back([this] { work(); });
    }
}

void Thread_pool::wait_all()
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv_finished.wait(lock , [this] {
        return tasks.empty() && active_threads == 0 || is_active_flag.load();
    });
}

bool Thread_pool::is_active()
{
    std::lock_guard<std::shared_mutex> lock(m_rwMutex);
    return is_active_flag.load(); 
}


int Thread_pool::return_scheduled_tasks()
{
    std::lock_guard<std::shared_mutex> lock(m_rwMutex);
    return scheduled_tasks;
}

void Thread_pool::set_scheduled_tasks(uint32_t num)
{
    this->scheduled_tasks += num;
}

void Thread_pool::clear_scheduled_tasks()
{
    scheduled_tasks = 0;
}

void Thread_pool::work() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            
            // 用原子操作避免竞态条件
            if (tasks.empty() && active_threads == 0) {
                cv_finished.notify_one();
            }

            cv_notify.wait(lock, [this] {
                return !tasks.empty() || is_active_flag.load();  // 使用 atomic<bool>
            });

            if (is_active_flag.load()) {  // 原子读取
                if (tasks.empty() && active_threads == 0) {
                    cv_finished.notify_one();
                }
                return;
            }

            if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
                active_threads++;  // 如果是 atomic，这里安全
            }
        }

        try {
            if (task) task();  // 捕获异常，避免线程崩溃
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            active_threads--;  // 确保异常时仍递减
            throw;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            active_threads--;
        }
    }
}