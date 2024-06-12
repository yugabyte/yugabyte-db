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

#include <atomic>
#include <mutex>

#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

#include "yb/gutil/casts.h"

#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/thread.h"

DEFINE_test_flag(bool, pg_client_crash_on_shared_memory_send, false,
                 "Crash while performing pg client send via shared memory.");

DEFINE_test_flag(bool, skip_remove_tserver_shared_memory_object, false,
                 "Skip remove tserver shared memory object in tests.");

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

#if defined(BOOST_INTERPROCESS_POSIX_PROCESS_SHARED)
class Semaphore {
 public:
  explicit Semaphore(unsigned int initial_count) {
    int ret = sem_init(&impl_, 1, initial_count);
    CHECK_NE(ret, -1);
  }

  Semaphore(const Semaphore&) = delete;
  void operator=(const Semaphore&) = delete;

  ~Semaphore() {
    CHECK_EQ(sem_destroy(&impl_), 0);
  }

  Status Post() {
    return ResToStatus(sem_post(&impl_), "Post");
  }

  Status Wait() {
    return ResToStatus(sem_wait(&impl_), "Wait");
  }

  template<class TimePoint>
  Status TimedWait(const TimePoint &abs_time) {
    // Posix does not support infinity absolute time so handle it here
    if (boost::interprocess::ipcdetail::is_pos_infinity(abs_time)) {
      return Wait();
    }

    auto tspec = boost::interprocess::ipcdetail::timepoint_to_timespec(abs_time);
    int res = sem_timedwait(&impl_, &tspec);
    if (res == 0) {
      return Status::OK();
    }
    if (res > 0) {
      // buggy glibc, copy the returned error code to errno
      errno = res;
    }
    if (errno == ETIMEDOUT) {
      static const Status timed_out_status = STATUS(TimedOut, "Timed out waiting semaphore");
      return timed_out_status;
    }
    if (errno == EINTR) {
      return Status::OK();
    }
    return ResToStatus(res, "TimedWait");
  }

 private:
  static Status ResToStatus(int res, const char* op) {
    if (res == 0) {
      return Status::OK();
    }
    return STATUS_FORMAT(RuntimeError, "$0 on semaphore failed: $1", op, errno);
  }

  sem_t impl_;
};
#else
class Semaphore {
 public:
  explicit Semaphore(unsigned int initial_count) : impl_(initial_count) {
  }

  Status Post() {
    impl_.post();
    return Status::OK();
  }

  Status Wait() {
    impl_.wait();
    return Status::OK();
  }

  template<class TimePoint>
  Status TimedWait(const TimePoint &abs_time) {
    if (!impl_.timed_wait(abs_time)) {
      static const Status timed_out_status = STATUS(TimedOut, "Timed out waiting semaphore");
      return timed_out_status;
    }
    return Status::OK();
  }

 private:
  boost::interprocess::interprocess_semaphore impl_;
};
#endif

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

  bool ReadyToSend(bool failed_previous_request) const {
    return ReadyToSend(state_.load(std::memory_order_acquire), failed_previous_request);
  }

  bool ReadyToSend(SharedExchangeState state, bool failed_previous_request) const {
    // Could use this exchange for sending request in two cases:
    // 1) it is idle, i.e. no request is being processed at this moment.
    // 2) the previous request was failed, and we received response for this request.
    return state == SharedExchangeState::kIdle ||
           (failed_previous_request && state == SharedExchangeState::kResponseSent);
  }

  Status SendRequest(bool failed_previous_request, size_t size) {
    auto state = state_.load(std::memory_order_acquire);
    if (!ReadyToSend(failed_previous_request)) {
      return STATUS_FORMAT(IllegalState, "Send request in wrong state: $0", state);
    }
    if (ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_pg_client_crash_on_shared_memory_send)) {
      LOG(FATAL) << "For test: crashing while sending request";
    }
    state_.store(SharedExchangeState::kRequestSent, std::memory_order_release);
    data_size_ = size;
    return request_semaphore_.Post();
  }

  bool ResponseReady() {
    return state_.load(std::memory_order_acquire) == SharedExchangeState::kResponseSent;
  }

  Result<size_t> FetchResponse(std::chrono::system_clock::time_point deadline) {
    RETURN_NOT_OK(DoWait(SharedExchangeState::kResponseSent, deadline, &response_semaphore_));
    state_.store(SharedExchangeState::kIdle, std::memory_order_release);
    return data_size_;
  }

  void Respond(size_t size) {
    auto state = state_.load(std::memory_order_acquire);
    if (state != SharedExchangeState::kRequestSent) {
      LOG_IF(DFATAL, state != SharedExchangeState::kShutdown)
          << "Respond in wrong state: " << AsString(state);
      return;
    }

    data_size_ = size;
    state_.store(SharedExchangeState::kResponseSent, std::memory_order_release);
    WARN_NOT_OK(response_semaphore_.Post(), "Respond failed");
  }

  Result<size_t> Poll() {
    RETURN_NOT_OK(DoWait(
        SharedExchangeState::kRequestSent, std::chrono::system_clock::time_point::max(),
        &request_semaphore_));
    return data_size_;
  }

  void SignalStop() {
    state_.store(SharedExchangeState::kShutdown, std::memory_order_release);
    WARN_NOT_OK(request_semaphore_.Post(), "SignalStop failed");
  }

 private:
  Status DoWait(
      SharedExchangeState expected_state,
      std::chrono::system_clock::time_point deadline,
      Semaphore* semaphore) {
    auto state = state_.load(std::memory_order_acquire);
    for (;;) {
      if (state == SharedExchangeState::kShutdown) {
        return STATUS_FORMAT(ShutdownInProgress, "Shutting down shared exchange");
      }
      auto wait_status = semaphore->TimedWait(deadline);
      state = state_.load(std::memory_order_acquire);
      if (state == expected_state) {
        return Status::OK();
      }
      if (wait_status.IsTimedOut()) {
        return STATUS_FORMAT(TimedOut, "Timed out waiting $0, state: $1", expected_state, state);
      }
      RETURN_NOT_OK(wait_status);
    }
  }

  Semaphore request_semaphore_{0};
  Semaphore response_semaphore_{0};
  std::atomic<SharedExchangeState> state_{SharedExchangeState::kIdle};
  size_t data_size_;
  std::byte data_[0];
};

std::string MakeSharedMemoryPrefix(const std::string& instance_id) {
  return Format("yb_pg_$0_", instance_id);
}

std::string MakeSharedMemoryName(const std::string& instance_id, uint64_t session_id) {
  return MakeSharedMemoryPrefix(instance_id) + std::to_string(session_id);
}

} // namespace

class SharedExchange::Impl {
 public:
  template <class T>
  Impl(T type, const std::string& instance_id, uint64_t session_id)
      : session_id_(session_id),
        owner_(std::is_same_v<T, boost::interprocess::create_only_t>),
        shared_memory_object_(type, MakeSharedMemoryName(instance_id, session_id).c_str(),
                              boost::interprocess::read_write) {
    if (owner_) {
      shared_memory_object_.truncate(boost::interprocess::mapped_region::get_page_size());
    }
    mapped_region_ = boost::interprocess::mapped_region(
        shared_memory_object_, boost::interprocess::read_write);
    if (owner_) {
      new (mapped_region_.get_address()) SharedExchangeHeader();
    }
  }

  ~Impl() {
    if (!owner_ || FLAGS_TEST_skip_remove_tserver_shared_memory_object) {
      return;
    }
    std::string shared_memory_object_name(shared_memory_object_.get_name());
    shared_memory_object_ = boost::interprocess::shared_memory_object();
    boost::interprocess::shared_memory_object::remove(shared_memory_object_name.c_str());
  }

  std::byte* Obtain(size_t required_size) {
    last_size_ = required_size;
    auto* header = &this->header();
    required_size += header->header_size();
    auto region_size = mapped_region_.get_size();
    if (required_size > region_size) {
      return nullptr;
    }
    return header->data();
  }

  uint64_t session_id() const {
    return session_id_;
  }

  Status SendRequest() {
    return header().SendRequest(failed_previous_request_, last_size_);
  }

  bool ResponseReady() {
    return header().ResponseReady();
  }

  Result<Slice> FetchResponse(CoarseTimePoint deadline) {
    auto& header = this->header();
    auto size_res = header.FetchResponse(ToSystem(deadline));
    if (!size_res.ok()) {
      failed_previous_request_ = true;
      return size_res.status();
    }
    failed_previous_request_ = false;
    if (*size_res + header.header_size() > mapped_region_.get_size()) {
      return Slice(static_cast<const char*>(nullptr), bit_cast<const char*>(*size_res));
    }
    return Slice(header.data(), *size_res);
  }

  bool ReadyToSend() const {
    return header().ReadyToSend(failed_previous_request_);
  }

  void Respond(size_t size) {
    header().Respond(size);
  }

  Result<size_t> Poll() {
    return header().Poll();
  }

  void SignalStop() {
    header().SignalStop();
  }

 private:
  SharedExchangeHeader& header() {
    return *static_cast<SharedExchangeHeader*>(mapped_region_.get_address());
  }

  const SharedExchangeHeader& header() const {
    return *static_cast<SharedExchangeHeader*>(mapped_region_.get_address());
  }

  const uint64_t session_id_;
  const bool owner_;
  boost::interprocess::shared_memory_object shared_memory_object_;
  boost::interprocess::mapped_region mapped_region_;
  size_t last_size_;
  bool failed_previous_request_ = false;
};

SharedExchange::SharedExchange(const std::string& instance_id, uint64_t session_id, Create create) {
  try {
    if (create) {
      impl_ = std::make_unique<Impl>(boost::interprocess::create_only, instance_id, session_id);
    } else {
      impl_ = std::make_unique<Impl>(boost::interprocess::open_only, instance_id, session_id);
    }
  } catch (boost::interprocess::interprocess_exception& exc) {
    LOG(FATAL) << "Failed to create shared exchange for " << instance_id << "/" << session_id
               << ", mode: " << create << ", error: " << exc.what();
  }
}

SharedExchange::~SharedExchange() = default;

Status SharedExchange::Cleanup(const std::string& instance_id) {
  std::string dir;
#if defined(BOOST_INTERPROCESS_POSIX_SHARED_MEMORY_OBJECTS)
  dir = "/dev/shm";
#else
  boost::interprocess::ipcdetail::get_shared_dir(dir);
#endif
  auto& env = *Env::Default();
  auto files = VERIFY_RESULT(env.GetChildren(dir, ExcludeDots::kTrue));
  auto prefix = MakeSharedMemoryPrefix(instance_id);
  for (const auto& file : files) {
    if (boost::starts_with(file, prefix)) {
      boost::interprocess::shared_memory_object::remove(file.c_str());
    }
  }
  return Status::OK();
}

std::byte* SharedExchange::Obtain(size_t required_size) {
  return impl_->Obtain(required_size);
}

Status SharedExchange::SendRequest() {
  return impl_->SendRequest();
}

bool SharedExchange::ResponseReady() const {
  return impl_->ResponseReady();
}

Result<Slice> SharedExchange::FetchResponse(CoarseTimePoint deadline) {
  return impl_->FetchResponse(deadline);
}

bool SharedExchange::ReadyToSend() const {
  return impl_->ReadyToSend();
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
    const std::string& instance_id, uint64_t session_id, Create create,
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

bool TServerSharedData::IsCronLeader() const {
  // We are the leader if we have a valid lease time that has not expired.
  auto lease_end = cron_leader_lease_.load();
  return lease_end.Initialized() && lease_end > MonoTime::Now();
}
} // namespace yb::tserver
