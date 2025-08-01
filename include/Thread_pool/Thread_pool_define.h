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
    
    std::vector<std::thread> threads ;
    std::queue<std::function<void()>> tasks ;
    std::queue<std::vector<uint8_t>> payload_queue ;
    std::mutex send_mutex;
    std::mutex queue_mutex ;
    std::condition_variable cv_send_queue;
    std::condition_variable cv_notify;
    std::condition_variable cv_finished;
    std::atomic<size_t> active_threads = 0 ;
    std::atomic<uint32_t> scheduled_tasks = 0;
    std::shared_mutex m_rwMutex;
    std::atomic<bool> is_active_flag = false;
};

template <typename F>
inline auto Thread_pool::enqueue(F &&f) -> std::future<decltype(f())>
{
    using return_type = decltype(f());

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::forward<F>(f)
    );

    std::future<return_type> res = task -> get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if (is_active_flag)
        {
            std::promise<return_type> promise;
            promise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error(
                        "Thread_pool has been shut down."
                    )
                )
            );
            return promise.get_future();
        }

        tasks.emplace(
            [task](){
                (*task)();
            }
        );
    }
    cv_notify.notify_one();
    return res;
}
