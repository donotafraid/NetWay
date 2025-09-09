#include "Thread_pool/Thread_pool_define.h"

Thread_pool::~Thread_pool()
{
    {
        //阻碍从任务队列里取出任务
        std::function<void()> lam ;
        std::lock_guard<std::mutex> lock(queue_mutex);
        is_inactive_flag.store(true,std::memory_order_release)  ;
        while(1)
        {
            if(tasks.try_dequeue(lam))
            {
                continue;
            }
            else
            {
                break;
            }
        }
    }

    //通知其他线程完成各自剩余的任务
    cv_notify.notify_all();

    for (auto &t : threads)
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
        return  pending_tasks.load() == 0 ;
    });
}

bool Thread_pool::is_active()
{
    return is_inactive_flag.load(); 
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
    while (!is_inactive_flag.load(std::memory_order_acquire)) {
        std::function<void()> task;
        {
        if(tasks.try_dequeue(task))
        {
        } 
        else 
        {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv_notify.wait(lock,[this]
            {
                // 满足条件则被唤醒，否则陷入休眠
                // ||关闭线程池标志的目的是：在关闭的时候，让线程能够退出循环，并结束线程
                return pending_tasks.load(std::memory_order_acquire) > 0 || (is_inactive_flag.load(std::memory_order_acquire)) ;
            }
        ); 

        // 唤醒后立即尝试获取任务,避免无效唤醒
        tasks.try_dequeue(task);
        }

        try {
                if (task) task();  // 捕获异常，避免线程崩溃
            }
        catch (...) {
                throw;
            }
        }
    }
}