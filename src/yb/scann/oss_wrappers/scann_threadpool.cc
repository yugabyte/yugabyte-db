// Copyright 2025 The Google Research Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "scann/oss_wrappers/scann_threadpool.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"

namespace research_scann {

class ThreadPool::Impl {
 public:
  explicit Impl(int num_threads) : num_threads_(num_threads), done_(false) {
    workers_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
      workers_.emplace_back(&Impl::WorkerLoop, this);
    }
  }

  ~Impl() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      done_ = true;
    }
    cv_.notify_all();
    for (std::thread& t : workers_) {
      t.join();
    }
  }

  void Schedule(std::function<void()> fn) {
    CHECK(fn != nullptr);
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (done_) return;
      queue_.push(std::move(fn));
    }
    cv_.notify_one();
  }

  int NumThreads() const { return num_threads_; }

 private:
  void WorkerLoop() {
    for (;;) {
      std::function<void()> fn;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this] { return done_ || !queue_.empty(); });
        if (done_ && queue_.empty()) return;
        if (!queue_.empty()) {
          fn = std::move(queue_.front());
          queue_.pop();
        }
      }
      if (fn) fn();
    }
  }

  const int num_threads_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> queue_;
  bool done_;
  std::vector<std::thread> workers_;
};

ThreadPool::ThreadPool(absl::string_view name, int num_threads)
    : impl_(std::make_unique<Impl>(num_threads)) {}

ThreadPool::~ThreadPool() = default;

void ThreadPool::Schedule(std::function<void()> fn) { impl_->Schedule(std::move(fn)); }

int ThreadPool::NumThreads() const { return impl_->NumThreads(); }

}  // namespace research_scann
