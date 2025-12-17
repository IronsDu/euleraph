#pragma once

#include <mutex>
#include <condition_variable>

class WaitGroup
{
public:
    void add(int count = 1)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        counter_ += count;
    }

    void done()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        --counter_;
        if (counter_ <= 0)
        {
            cv_.notify_all();
        }
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return counter_ <= 0; });
    }

private:
    std::mutex              mtx_;
    std::condition_variable cv_;
    int                     counter_ = 0;
};