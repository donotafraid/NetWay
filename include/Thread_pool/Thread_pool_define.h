#pragma once

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <queue>
#include <functional>
#include <execution>

#include "load_config/load_config.h"
#include "ConnectionPool.h"

class Thread_pool
{
    public:
    Thread_pool() = default ;
    ~Thread_pool()  ;

    void start();
    template<typename F>
    auto enqueue(F&& f) -> std::future<decltype(f())>;
    void wait_all();
    bool is_active();
    int return_scheduled_tasks();
    void set_scheduled_tasks(uint32_t num);
    void clear_scheduled_tasks();

    std::unique_ptr<ConnectionPool> connection_pool_ptr = nullptr;
    private:
    void work();
    
    moodycamel::ConcurrentQueue <std::function<void()>> tasks; 
    std::vector<std::thread> threads ;
    std::mutex send_mutex;
    std::mutex queue_mutex ;
    std::condition_variable cv_send_queue;
    std::condition_variable cv_notify;
    std::condition_variable cv_finished;
    std::atomic<size_t> pending_tasks = 0 ;
    std::atomic<uint32_t> scheduled_tasks = 0;
    std::shared_mutex m_rwMutex;
    std::atomic<bool> is_inactive_flag = false;
};

template <typename F>
inline auto Thread_pool::enqueue(F &&f) -> std::future<decltype(f())>
{
    using return_type = decltype(f());
    //  揭示函数返回值类型（如 int/float/void）
    //  记录函数调用特征（无参数 `()`）
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::forward<F>(f)
    );
    //  用 decltype +  package_task 封装，规范了它的身份(标准化为固定尺寸的函数指针+控制块,无锁队列最擅长处理统一尺寸元素（预分配内存池，避免碎片）)
    //  用 forward 确保了该函数对象在不同场景下的行为可行
    //  用 shared_ptr 规范了该任务的生命周期

    std::future<return_type> res = task -> get_future();
    pending_tasks.fetch_add(1,std::memory_order_release);
    tasks.enqueue([task,this](){ 
        (*task)();
        // fetch_sub 的行为：
        // 原子地将 pending_tasks 的值减 1。
        // 返回减操作前的旧值（即递减前的任务数量）。
        // relaxed ：只保证原子性，不保证了数据的即时性
        if (pending_tasks.fetch_sub(1,std::memory_order_relaxed) == 1)
        {
            std::lock_guard lock(queue_mutex);
            cv_finished.notify_all();
        }
    });

    //虚假唤醒：指线程被唤醒但条件并未满足（如队列空），导致重新休眠->线程再次被唤醒，演变出了虚假唤醒风暴
    if(pending_tasks.load(std::memory_order_relaxed)>threads.size()/2)
    {
        cv_notify.notify_all();
    }
    else
    {
        cv_notify.notify_one();
    }
    return res;
}

