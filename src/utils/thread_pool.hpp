#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>
#include <memory>

// 开启指定数量（必须大于0）的线程，执行用户所压入的任务函数。

// 线程池不提供主动停止的接口，是因为线程池一般总是在其他模块结束工作后才销毁。
// 反而，如果提供停止的接口，那么其他模块在压入任务的时候就需要判断是否成功。

// 长时间的耗时任务和短任务可以放在不同的线程池去执行。
// 此外，长时间的耗时任务尽量定期自己判断是否需要退出。线程池本身无法取消或终止任务。
class ThreadPool
{
public:
    using Ptr = std::shared_ptr<ThreadPool>;

public:
    ThreadPool(size_t numThreads, std::optional<std::function<void()>> cb = std::nullopt)
        : stop_(false), work_exit_callback_(cb)
    {
        if (numThreads == 0)
        {
            throw std::runtime_error("thread num is zero");
        }
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers_.emplace_back([this] { worker(); });
        }
    }

    // 析构函数会等待所有工作线程在执行完所有任务后才会退出。
    virtual ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            stop_ = true;
            tasks_condition_.notify_all();
        }
        for (std::thread& worker : workers_)
        {
            worker.join();
        }
    }

    void enqueue(std::function<void()> func)
    {
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_.emplace(std::move(func));
        }
        tasks_condition_.notify_one();
    }

private:
    void worker()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(tasks_mutex_);
                tasks_condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                // 只有被通知结束和任务为空才退出。也就是说：在有任务的情况下，会将所有任务执行完。
                if (stop_ && tasks_.empty())
                    break;

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
        if (work_exit_callback_)
        {
            work_exit_callback_.value();
        }
    }

private:
    std::vector<std::thread>             workers_;
    std::optional<std::function<void()>> work_exit_callback_;

    std::queue<std::function<void()>> tasks_;
    std::mutex                        tasks_mutex_;
    std::condition_variable           tasks_condition_;

    // 是否正在停止线程池的工作。目前线程池只有在销毁时才会停止。
    bool stop_ = {false};
};