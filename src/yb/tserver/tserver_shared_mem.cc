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

#include "yb/tserver/tserver_shared_mem.h"

#include <mutex>

#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/thread.h"

namespace yb::tserver {

namespace {

using SystemClock = std::chrono::system_clock;

std::chrono::system_clock::time_point ToSystemBase() {
  auto now_system = SystemClock::now();
  auto coarse_since_epoch = CoarseMonoClock::now().time_since_epoch();
  return now_system - std::chrono::duration_cast<SystemClock::duration>(coarse_since_epoch);
}

std::chrono::system_clock::time_point ToSystem(CoarseTimePoint tp) {
  static SystemClock::time_point base = ToSystemBase();
  if (tp == CoarseTimePoint()) {
    return SystemClock::time_point::max();
  }
  return base + std::chrono::duration_cast<SystemClock::duration>(tp.time_since_epoch());
}

YB_DEFINE_ENUM(SharedExchangeState,
               (kIdle)(kRequestSent)(kResponseSent)(kShutdown));

class SharedExchangeHeader {
 public:
  SharedExchangeHeader() = default;

  std::byte* data() {
    return data_;
  }

  size_t header_size() {
    return data() - pointer_cast<std::byte*>(this);
  }

  Result<size_t> SendRequest(
      boost::interprocess::message_queue* message_queue, uint64_t session_id,
      size_t size, std::chrono::system_clock::time_point deadline) {
    std::unique_lock<boost::interprocess::interprocess_mutex> lock(mutex_);
    auto state = state_;
    if (state != SharedExchangeState::kIdle) {
      lock.unlock();
      return STATUS_FORMAT(IllegalState, "Send request in wrong state: $0", state);
    }
    state_ = SharedExchangeState::kRequestSent;
    data_size_ = size;
    cond_.notify_one();

    RETURN_NOT_OK(DoWait(SharedExchangeState::kResponseSent, deadline, &lock));
    state_ = SharedExchangeState::kIdle;
    return data_size_;
  }

  void Respond(size_t size) {
    std::unique_lock<boost::interprocess::interprocess_mutex> lock(mutex_);
    auto state = state_;
    if (state != SharedExchangeState::kRequestSent) {
      lock.unlock();
      LOG_IF(DFATAL, state != SharedExchangeState::kShutdown)
          << "Respond in wrong state: " << AsString(state);
      return;
    }

    data_size_ = size;
    state_ = SharedExchangeState::kResponseSent;
    cond_.notify_one();
  }

  Result<size_t> Poll() {
    std::unique_lock<boost::interprocess::interprocess_mutex> lock(mutex_);
    RETURN_NOT_OK(DoWait(
        SharedExchangeState::kRequestSent, std::chrono::system_clock::time_point::max(), &lock));
    return data_size_;
  }

  void SignalStop() {
    std::unique_lock<boost::interprocess::interprocess_mutex> lock(mutex_);
    state_ = SharedExchangeState::kShutdown;
    cond_.notify_all();
  }

 private:
  Status DoWait(SharedExchangeState expected_state, std::chrono::system_clock::time_point deadline,
                std::unique_lock<boost::interprocess::interprocess_mutex>* lock) {
    for (;;) {
      if (state_ == expected_state) {
        return Status::OK();
      }
      if (state_ == SharedExchangeState::kShutdown) {
        lock->unlock();
        return STATUS_FORMAT(ShutdownInProgress, "Shutting down shared exchange");
      }
      if (!cond_.timed_wait(*lock, deadline)) {
        auto state = state_;
        lock->unlock();
        return STATUS_FORMAT(TimedOut, "Timed out waiting $0, state: $1", expected_state, state);
      }
    }
  }

  boost::interprocess::interprocess_mutex mutex_;
  boost::interprocess::interprocess_condition cond_;
  SharedExchangeState state_ = SharedExchangeState::kIdle;
  size_t data_size_;
  std::byte data_[0];
};

std::string MakeSharedMemoryName(const Uuid& instance_id, uint64_t session_id) {
  return Format("yb_pg_$0_$1", instance_id, session_id);
}

} // namespace

class SharedExchange::Impl {
 public:
  template <class T>
  Impl(T type, const Uuid& instance_id, uint64_t session_id)
      : session_id_(session_id),
        shared_memory_object_(type, MakeSharedMemoryName(instance_id, session_id).c_str(),
                              boost::interprocess::read_write) {
    constexpr auto create = std::is_same_v<T, boost::interprocess::create_only_t>;
    if (create) {
      shared_memory_object_.truncate(boost::interprocess::mapped_region::get_page_size());
    }
    mapped_region_ = boost::interprocess::mapped_region(
        shared_memory_object_, boost::interprocess::read_write);
    if (create) {
      new (mapped_region_.get_address()) SharedExchangeHeader();
    }
  }

  std::byte* Obtain(size_t required_size) {
    last_size_ = required_size;
    auto* header = this->header();
    required_size += header->header_size();
    auto region_size = mapped_region_.get_size();
    if (required_size > region_size) {
      auto page_size = boost::interprocess::mapped_region::get_page_size();
      auto new_size = ((required_size + page_size - 1) / page_size) * page_size;
      shared_memory_object_.truncate(new_size);
      Reopen();
      header = this->header();
    }
    return header->data();
  }

  uint64_t session_id() const {
    return session_id_;
  }

  Result<Slice> SendRequest(
      boost::interprocess::message_queue* message_queue, CoarseTimePoint deadline) {
    auto* header = this->header();
    auto size = VERIFY_RESULT(header->SendRequest(
        message_queue, session_id_, last_size_, ToSystem(deadline)));
    if (size + header->header_size() > mapped_region_.get_size()) {
      Reopen();
      header = this->header();
    }
    return Slice(header->data(), size);
  }

  void Respond(size_t size) {
    header()->Respond(size);
  }

  Result<size_t> Poll() {
    return header()->Poll();
  }

  void SignalStop() {
    header()->SignalStop();
  }

 private:
  SharedExchangeHeader* header() {
    return static_cast<SharedExchangeHeader*>(mapped_region_.get_address());
  }

  void Reopen() {
    mapped_region_ = boost::interprocess::mapped_region();
    mapped_region_ = boost::interprocess::mapped_region(
        shared_memory_object_, boost::interprocess::read_write);
  }

  const uint64_t session_id_;
  boost::interprocess::shared_memory_object shared_memory_object_;
  boost::interprocess::mapped_region mapped_region_;
  size_t last_size_;
};

SharedExchange::SharedExchange(const Uuid& instance_id, uint64_t session_id, Create create) {
  if (create) {
    impl_ = std::make_unique<Impl>(boost::interprocess::create_only, instance_id, session_id);
  } else {
    impl_ = std::make_unique<Impl>(boost::interprocess::open_only, instance_id, session_id);
  }
}

SharedExchange::~SharedExchange() = default;

std::byte* SharedExchange::Obtain(size_t required_size) {
  return impl_->Obtain(required_size);
}

Result<Slice> SharedExchange::SendRequest(
    boost::interprocess::message_queue* message_queue, CoarseTimePoint deadline) {
  return impl_->SendRequest(message_queue, deadline);
}

void SharedExchange::Respond(size_t size) {
  return impl_->Respond(size);
}

Result<size_t> SharedExchange::Poll() {
  return impl_->Poll();
}

void SharedExchange::SignalStop() {
  impl_->SignalStop();
}

uint64_t SharedExchange::session_id() const {
  return impl_->session_id();
}

SharedExchangeThread::SharedExchangeThread(
    const Uuid& instance_id, uint64_t session_id, Create create,
    const SharedExchangeListener& listener)
    : exchange_(instance_id, session_id, create) {
  CHECK_OK(Thread::Create(
      "shared_exchange", Format("sh_xchng_$0", session_id), [this, listener] {
    CDSAttacher cdc_attacher;
    for (;;) {
      auto query_size = exchange_.Poll();
      if (!query_size.ok()) {
        if (!query_size.status().IsShutdownInProgress()) {
          LOG(DFATAL) << "Poll session " << exchange_.session_id() <<  " failed: "
                      << query_size.status();
        }
        break;
      }
      listener(*query_size);
    }
  }, &thread_));
}

SharedExchangeThread::~SharedExchangeThread() {
  exchange_.SignalStop();
  thread_->Join();
}

} // namespace yb::tserver
