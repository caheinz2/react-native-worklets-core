#include "DispatchQueue.h"

namespace RNWorklet {

JsiDispatchQueue::~JsiDispatchQueue()
{
    printf("Destructor: Destroying dispatch threads...\n");

    // Signal to dispatch threads that it's time to wrap up
    std::unique_lock<std::mutex> lock(lock_);
    quit_ = true;
    lock.unlock();
    cv_.notify_all();

    // Wait for threads to finish before we exit
    for(size_t i = 0; i < threads_.size(); i++)
    {
        if(threads_[i].joinable())
        {
            printf("Destructor: Joining thread %zu until completion\n", i);
            threads_[i].join();
        }
    }
}

JsiDispatchQueue::JsiDispatchQueue(std::string name, size_t thread_cnt) :
        name_{std::move(name)}, threads_(thread_cnt)
{
    printf("Creating dispatch queue: %s\n", name_.c_str());
    printf("Dispatch threads: %zu\n", thread_cnt);

    for(size_t i = 0; i < threads_.size(); i++)
    {
        threads_[i] = std::thread(&JsiDispatchQueue::dispatch_thread_handler, this);
    }
}

void JsiDispatchQueue::dispatch(const fp_t& op)
{
    std::unique_lock<std::mutex> lock(lock_);
    q_.push(op);

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    lock.unlock();
    cv_.notify_one();
}

void JsiDispatchQueue::dispatch(fp_t&& op)
{
    std::unique_lock<std::mutex> lock(lock_);
    q_.push(std::move(op));

    // Manual unlocking is done before notifying, to avoid waking up
    // the waiting thread only to block again (see notify_one for details)
    lock.unlock();
    cv_.notify_one();
}

void JsiDispatchQueue::dispatch_thread_handler(void)
{
    std::unique_lock<std::mutex> lock(lock_);

    do
    {
        // Wait until we have data or a quit signal
        cv_.wait(lock, [this] {
            return (q_.size() || quit_);
        });

        // after wait, we own the lock
        if(!quit_ && q_.size())
        {
            auto op = std::move(q_.front());
            q_.pop();

            // unlock now that we're done messing with the queue
            lock.unlock();

            op();

            lock.lock();
        }
    } while(!quit_);
}
}