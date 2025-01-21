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

#include <boost/interprocess/shared_memory_object.hpp>

#include "yb/gutil/casts.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shmem/interprocess_semaphore.h"
#include "yb/util/shmem/shared_mem_segment.h"
#include "yb/util/size_literals.h"
#include "yb/util/thread.h"
#include "yb/util/uuid.h"

DEFINE_RUNTIME_uint64(ts_shared_memory_setup_max_wait_ms, 10000,
                      "Maximum wait time for tserver to set up shared memory state");

DEFINE_test_flag(bool, pg_client_crash_on_shared_memory_send, false,
                 "Crash while performing pg client send via shared memory.");

DEFINE_test_flag(bool, skip_remove_tserver_shared_memory_object, false,
                 "Skip remove tserver shared memory object in tests.");

DECLARE_bool(TEST_enable_object_locking_for_table_locks);

using namespace std::literals;

namespace yb::tserver {

namespace {

using SystemClock = std::chrono::system_clock;

Result<std::string> MakeAllocatorName(std::string_view uuid) {
  // Hex-encoded uuid is too long for allocator name, so base64 encoding is used.
  std::string uuid_bytes;
  VERIFY_RESULT(Uuid::FromHexStringBigEndian(std::string(uuid))).ToBytes(&uuid_bytes);
  std::string out;
  WebSafeBase64Escape(uuid_bytes, &out);
  return out;
}

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
               (kIdle)(kRequestSent)(kProcessingRequest)(kResponseSent)(kShutdown));

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
    if (!ReadyToSend(state, failed_previous_request)) {
      return STATUS_FORMAT(IllegalState, "Send request in wrong state: $0", state);
    }
    if (ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_pg_client_crash_on_shared_memory_send)) {
      LOG(FATAL) << "For test: crashing while sending request";
    }
    RETURN_NOT_OK(TransferState(state, SharedExchangeState::kRequestSent));
    data_size_ = size;
    return request_semaphore_.Post();
  }

  Status TransferState(SharedExchangeState old_state, SharedExchangeState new_state) {
    SharedExchangeState actual_state = old_state;
    if (state_.compare_exchange_strong(actual_state, new_state, std::memory_order_acq_rel)) {
      return Status::OK();
    }
    return STATUS_FORMAT(
        IllegalState, "Wrong state, $0 expected, but $1 found", old_state, actual_state);
  }

  bool ResponseReady() {
    return state_.load(std::memory_order_acquire) == SharedExchangeState::kResponseSent;
  }

  Result<size_t> FetchResponse(std::chrono::system_clock::time_point deadline) {
    RETURN_NOT_OK(DoWait(SharedExchangeState::kResponseSent, deadline, &response_semaphore_));
    RETURN_NOT_OK(TransferState(SharedExchangeState::kResponseSent, SharedExchangeState::kIdle));
    return data_size_;
  }

  void Respond(size_t size) {
    auto state = state_.load(std::memory_order_acquire);
    if (state != SharedExchangeState::kProcessingRequest) {
      LOG_IF(DFATAL, state != SharedExchangeState::kShutdown)
          << "Respond in wrong state: " << AsString(state);
      return;
    }

    data_size_ = size;
    WARN_NOT_OK(
        TransferState(SharedExchangeState::kProcessingRequest, SharedExchangeState::kResponseSent),
        "Transfer state failed");
    WARN_NOT_OK(response_semaphore_.Post(), "Respond failed");
  }

  Result<size_t> Poll() {
    RETURN_NOT_OK(DoWait(
        SharedExchangeState::kRequestSent, std::chrono::system_clock::time_point::max(),
        &request_semaphore_));
    RETURN_NOT_OK(TransferState(
        SharedExchangeState::kRequestSent, SharedExchangeState::kProcessingRequest));
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
      InterprocessSemaphore* semaphore) {
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

  InterprocessSemaphore request_semaphore_{0};
  InterprocessSemaphore response_semaphore_{0};
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

Status TServerSharedData::AllocatorsInitialized(SharedMemoryBackingAllocator& allocator) {
  fully_initialized_ = true;
  return Status::OK();
}

Status TServerSharedData::WaitAllocatorsInitialized() {
  return WaitFor(
      [this]() -> Result<bool> { return fully_initialized_; },
      FLAGS_ts_shared_memory_setup_max_wait_ms * 1ms,
      "Wait for shared memory allocators to be initialized");
}

SharedMemoryManager::~SharedMemoryManager() {
  CHECK_OK(ShutdownNegotiator());
  if (is_parent_ && data_) {
    data_->~TServerSharedData();
  }
}

Status SharedMemoryManager::InitializeTServer(std::string_view uuid) {
  is_parent_ = true;
  return PrepareAllocators(uuid);
}

Status SharedMemoryManager::PrepareNegotiationTServer() {
  if (parent_negotiator_thread_) {
    RETURN_NOT_OK(ShutdownNegotiator());
  }

  auto negotiator = std::make_shared<AddressSegmentNegotiator>();
  RETURN_NOT_OK(negotiator->PrepareNegotiation(&address_segment_));

  parent_negotiator_ = negotiator;
  parent_negotiator_thread_ = VERIFY_RESULT(Thread::Make(
      "SharedMemoryManager", "Negotiator",
      &SharedMemoryManager::ExecuteParentNegotiator, this, negotiator));

  return Status::OK();
}

Status SharedMemoryManager::SkipNegotiation() {
  address_segment_ = VERIFY_RESULT(AddressSegmentNegotiator::ReserveWithoutNegotiation());
  CHECK_OK(InitializeParentAllocatorsAndObjects());
  return Status::OK();
}

Status SharedMemoryManager::InitializePostmaster(int fd) {
  address_segment_ = VERIFY_RESULT(AddressSegmentNegotiator::NegotiateChild(fd));
  return Status::OK();
}

Status SharedMemoryManager::InitializePgBackend(std::string_view uuid) {
  bool pointer_support = address_segment_.Active();
  if (!pointer_support) {
    // Shared memory for the case of master and initdb processes does not have pointer support,
    // since there is no "parent" process like postmaster from which all PG processes are forked
    // from to do address negotiation with.
    LOG(INFO) << "Initializing shared memory without pointer support";
    address_segment_ = VERIFY_RESULT(AddressSegmentNegotiator::ReserveWithoutNegotiation());
  }
  RETURN_NOT_OK(InitializeChildAllocatorsAndObjects(uuid));
  if (pointer_support) {
    RETURN_NOT_OK(SharedData().WaitAllocatorsInitialized());
  }
  return Status::OK();
}

Status SharedMemoryManager::ShutdownNegotiator() {
  if (auto negotiator = parent_negotiator_.lock()) {
    RETURN_NOT_OK(negotiator->Shutdown());
  }
  if (parent_negotiator_thread_) {
    parent_negotiator_thread_->Join();
    parent_negotiator_thread_.reset();
  }
  return Status::OK();
}

int SharedMemoryManager::NegotiationFd() const {
  if (auto negotiator = parent_negotiator_.lock()) {
    return negotiator->GetFd();
  }
  return -1;
}

Status SharedMemoryManager::PrepareAllocators(std::string_view uuid) {
  if (ready_) {
    return Status::OK();
  }

  prepare_state_ = VERIFY_RESULT(allocator_.Prepare(
      VERIFY_RESULT(MakeAllocatorName(uuid)), sizeof(TServerSharedData)));
  data_ = new (prepare_state_.UserData()) TServerSharedData();
  return Status::OK();
}

void SharedMemoryManager::ExecuteParentNegotiator(
    const std::shared_ptr<AddressSegmentNegotiator>& negotiator) {
  auto result = negotiator->NegotiateParent();
  if (!result.ok()) {
    if (result.status().IsShutdownInProgress()) {
      return;
    }

    // FATAL is necessary here: in event of postmaster restart, negotiation may fail in the
    // (unlikely) situation where the address segment that we were using (and which has initialized
    // shared memory data structures) is unavailable on the newly started postmaster. In this case,
    // we make use of tserver restart to renegotiate addresses from scratch.
    LOG(FATAL) << "Address segment negotiation failed: " << result.status();
  }

  address_segment_ = std::move(*result);

  CHECK_OK(InitializeParentAllocatorsAndObjects());

  LOG(INFO) << "Finished address segment negotiation";
}

Status SharedMemoryManager::InitializeParentAllocatorsAndObjects() {
  if (ready_) {
    return Status::OK();
  }
  RETURN_NOT_OK(allocator_.InitOwner(address_segment_, std::move(prepare_state_)));
  data_ = allocator_.UserData<TServerSharedData>();
  RETURN_NOT_OK(data_->AllocatorsInitialized(allocator_));
  ready_.store(true);
  return Status::OK();
}

Status SharedMemoryManager::InitializeChildAllocatorsAndObjects(std::string_view uuid) {
  if (ready_) {
    return Status::OK();
  }
  RETURN_NOT_OK(allocator_.InitChild(address_segment_, VERIFY_RESULT(MakeAllocatorName(uuid))));
  data_ = allocator_.UserData<TServerSharedData>();
  ready_.store(true);
  return Status::OK();
}

class SharedExchange::Impl {
 public:
  template <class T>
  Impl(T type, const std::string& instance_id, uint64_t session_id)
      : instance_id_(instance_id),
        session_id_(session_id),
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

  const std::string& instance_id() const {
    return instance_id_;
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

  const std::string instance_id_;
  const uint64_t session_id_;
  const bool owner_;
  boost::interprocess::shared_memory_object shared_memory_object_;
  boost::interprocess::mapped_region mapped_region_;
  size_t last_size_;
  bool failed_previous_request_ = false;
};

Result<SharedExchange> SharedExchange::Make(
      const std::string& instance_id, uint64_t session_id, Create create) {
  try {
    std::unique_ptr<Impl> impl;
    if (create) {
      impl = std::make_unique<Impl>(boost::interprocess::create_only, instance_id, session_id);
    } else {
      impl = std::make_unique<Impl>(boost::interprocess::open_only, instance_id, session_id);
    }
    return SharedExchange(std::move(impl));
  } catch (boost::interprocess::interprocess_exception& exc) {
    auto result = STATUS_FORMAT(
        RuntimeError, "Failed to create shared exchange for $0/$1, mode: $2, error: $3",
        instance_id, session_id, create, exc.what());
    LOG(DFATAL) << result;
    return result;
  }
}

SharedExchange::SharedExchange(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
}

SharedExchange::SharedExchange(SharedExchange&& rhs) : impl_(std::move(rhs.impl_)) {
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

const std::string& SharedExchange::instance_id() const {
  return impl_->instance_id();
}

uint64_t SharedExchange::session_id() const {
  return impl_->session_id();
}

SharedExchangeThread::SharedExchangeThread(
    SharedExchange exchange,
    const SharedExchangeListener& listener)
    : exchange_(std::move(exchange)) {
  CHECK_OK(Thread::Create(
      "shared_exchange", Format("sh_xchng_$0", exchange_.session_id()), [this, listener] {
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
    ready_to_complete_ = true;
  }, &thread_));
}

SharedExchangeThread::~SharedExchangeThread() {
  StartShutdown();
  CompleteShutdown();
}

void SharedExchangeThread::StartShutdown() {
  if (thread_) {
    exchange_.SignalStop();
  }
}

void SharedExchangeThread::CompleteShutdown() {
  if (thread_) {
    thread_->Join();
    thread_ = nullptr;
  }
}

bool TServerSharedData::IsCronLeader() const {
  // We are the leader if we have a valid lease time that has not expired.
  auto lease_end = cron_leader_lease_.load();
  return lease_end.Initialized() && lease_end > MonoTime::Now();
}

std::string MakeSharedMemoryBigSegmentName(const std::string& instance_id, uint64_t id) {
  return MakeSharedMemoryPrefix(instance_id) + "_big_" + std::to_string(id);
}

} // namespace yb::tserver
