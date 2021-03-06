// GSLAM - A general SLAM framework and benchmark
// Copyright 2018 PILAB Inc. All rights reserved.
// https://github.com/zdzhaoyong/GSLAM
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: 1529901761@qq.com (tongpinmo)
//
// This is the GSLAM main API header

#ifndef GSLAM_CORE_MUTEX_H_
#define GSLAM_CORE_MUTEX_H_

#ifdef HAS_PIL0

#include <pil/base/Thread/Event.h>
#include <pil/base/Thread/Thread.h>

namespace GSLAM {

typedef pi::MutexRW MutexRW;
typedef pi::ReadMutex ReadMutex;
typedef pi::WriteMutex WriteMutex;
typedef pi::Event Event;
}

#else
#include <atomic>
#include <condition_variable>  // NOLINT
#include <functional>
#include <future>  // NOLINT
#include <mutex>   // NOLINT
#include <queue>
#include <thread>  //  NOLINT
#include <vector>
#include "Glog.h"

namespace GSLAM {
typedef std::mutex Mutex;
typedef std::mutex MutexRW;
typedef std::unique_lock<MutexRW> ReadMutex;
typedef std::unique_lock<MutexRW> WriteMutex;

class Event {
 public:
  explicit Event(bool autoReset = true) : _auto(autoReset), _state(false) {}
  ~Event() {}

  void set() { notify_all(); }

  void wait() {
    if (_auto) _state = false;
    if (!_state) {
      std::unique_lock<std::mutex> _lock(_mutex);
      _cond.wait(_lock);
    }
  }

  void notify_all() {
    _cond.notify_all();
    _state = true;
  }

  void notify_once() {
    _cond.notify_one();
    _state = true;
  }

  void reset() {
    std::unique_lock<std::mutex> _lock(_mutex);
    _state = false;
  }

 private:
  Event(const Event&);
  Event& operator=(const Event&);

  bool _auto;
  std::atomic<bool> _state;
  std::mutex _mutex;
  std::condition_variable _cond;
};

// A simple threadpool implementation.
class ThreadPool {
 public:
  // All the threads are created upon construction.
  explicit ThreadPool(const int num_threads) : stop(false) {
    CHECK_GE(num_threads, 1)
        << "The number of threads specified to the ThreadPool is insufficient.";
    for (size_t i = 0; i < num_threads; ++i) {
      workers.emplace_back([this] {
        for (;;) {
          std::function<void()> task;

          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty()) return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }

          task();
        }
      });
    }
  }
  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) worker.join();
  }

  // Adds a task to the threadpool.
  template <class F, class... Args>
  auto Add(F&& f, Args&&... args)
      -> std::future<typename std::result_of<F(Args...)>::type>;

 private:
  // Keep track of threads so we can join them
  std::vector<std::thread> workers;
  // The task queue
  std::queue<std::function<void()> > tasks;

  // Synchronization
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

// add new work item to the pool
template <class F, class... Args>
auto ThreadPool::Add(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared<std::packaged_task<return_type()> >(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    // don't allow enqueueing after stopping the pool
    CHECK(!stop) << "The ThreadPool object has been destroyed! Cannot add more "
                    "tasks to the ThreadPool!";

    tasks.emplace([task]() { (*task)(); });
  }
  condition.notify_one();
  return res;
}
}  // namespace GSLAM
#endif  // GSLAM_CORE_MUTEX_H_

#endif  // GSLAM_CORE_MUTEX_H_
