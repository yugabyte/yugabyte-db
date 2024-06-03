//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//

#include "yb/rpc/io_thread_pool.h"

#include <thread>

#include <boost/asio/io_service.hpp>
#include <boost/optional.hpp>
#include "yb/util/logging.h"

#include "yb/util/status_log.h"
#include "yb/util/thread.h"

using namespace std::literals;

namespace yb {
namespace rpc {

class IoThreadPool::Impl {
 public:
  Impl(const std::string& name, size_t num_threads) : name_(name) {
    threads_.reserve(num_threads);
    size_t index = 0;
    while (threads_.size() != num_threads) {
      threads_.push_back(CHECK_RESULT(Thread::Make(
          Format("iotp_$0", name_), Format("iotp_$0_$1", name_, index),
          std::bind(&Impl::Execute, this))));
      ++index;
    }
  }

  ~Impl() {
    Shutdown();
    Join();
  }

  IoService& io_service() {
    return io_service_;
  }

  void Shutdown() {
    work_.reset();
  }

  void Join() {
    auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!io_service_.stopped()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        LOG(ERROR) << "Io service failed to stop";
        io_service_.stop();
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    for (auto& thread : threads_) {
      thread->Join();
    }
  }

 private:
  void Execute() {
    boost::system::error_code ec;
    io_service_.run(ec);
    LOG_IF(ERROR, ec) << "Failed to run io service: " << ec;
  }

  std::string name_;
  std::vector<ThreadPtr> threads_;
  IoService io_service_;
  boost::optional<IoService::work> work_{io_service_};
};

IoThreadPool::IoThreadPool(const std::string& name, size_t num_threads)
    : impl_(new Impl(name, num_threads)) {}

IoThreadPool::~IoThreadPool() {}

IoService& IoThreadPool::io_service() {
  return impl_->io_service();
}

void IoThreadPool::Shutdown() {
  impl_->Shutdown();
}

void IoThreadPool::Join() {
  impl_->Join();
}

} // namespace rpc
} // namespace yb
