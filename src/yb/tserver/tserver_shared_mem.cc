// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/object_lock_shared_state.h"

#include "yb/gutil/casts.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
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

DEFINE_RUNTIME_bool(pg_client_use_shared_memory, !yb::kIsMac,
                    "Use shared memory for executing read and write pg client queries");

DEFINE_NON_RUNTIME_bool(enable_object_lock_fastpath, !yb::kIsMac,
    "Whether to use shared memory fastpath for shared object locks.");

DEFINE_validator(pg_client_use_shared_memory,
    FLAG_REQUIRED_BY_FLAG_VALIDATOR(enable_object_lock_fastpath));
DEFINE_validator(enable_object_lock_fastpath,
    FLAG_REQUIRES_FLAG_VALIDATOR(pg_client_use_shared_memory));

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

std::string MakeSharedMemoryPrefix(std::string_view instance_id) {
  return Format("yb_pg_$0_", instance_id);
}

std::string MakeSharedMemoryName(std::string_view instance_id, uint64_t session_id) {
  return MakeSharedMemoryPrefix(instance_id) + std::to_string(session_id);
}

} // namespace

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
    for (auto* semaphore : {&request_semaphore_, &response_semaphore_}) {
      WARN_NOT_OK(semaphore->Post(), "SignalStop failed");
    }
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

namespace {

struct PgSessionSharedHeader {
  PgSessionLockOwnerTagShared object_locking_data;

  // This must be the last field, since it uses a zero length array.
  SharedExchangeHeader exchange_header;
};

} // namespace

TServerSharedData::TServerSharedData() {
  // All atomics stored in shared memory must be lock-free. Non-robust locks
  // in shared memory can lead to deadlock if a processes crashes, and memory
  // access violations if the segment is mapped as read-only.
  // NOTE: this check is NOT sufficient to guarantee that an atomic is safe
  // for shared memory! Some atomics claim to be lock-free but still require
  // read-write access for a `load()`.
  // E.g. for 128 bit objects: https://stackoverflow.com/questions/49816855.
  LOG_IF(FATAL, !IsAcceptableAtomicImpl(catalog_version_))
      << "Shared memory atomics must be lock-free";
  host_[0] = 0;
}

TServerSharedData::~TServerSharedData() = default;

Status TServerSharedData::AllocatorsInitialized(SharedMemoryBackingAllocator& allocator) {
  if (FLAGS_enable_object_lock_fastpath) {
    object_lock_state_ =
        VERIFY_RESULT(allocator.MakeUnique<docdb::ObjectLockSharedState>(allocator));
  }

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
  auto data = data_.get();
  if (is_parent_ && data) {
    data->~TServerSharedData();
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
  return InitializeParentAllocatorsAndObjects();
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
    RETURN_NOT_OK(SharedData()->WaitAllocatorsInitialized());
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
  data_.Set(new (prepare_state_.UserData()) TServerSharedData());
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

void SharedMemoryManager::SetReadyCallback(SharedMemoryReadyCallback&& callback) {
  {
    std::lock_guard lock(mutex_);
    if (!ready_) {
      ready_callback_ = std::move(callback);
      return;
    }
  }
  callback();
}

Status SharedMemoryManager::InitializeParentAllocatorsAndObjects() {
  if (ready_) {
    return Status::OK();
  }
  RETURN_NOT_OK(allocator_.InitOwner(address_segment_, std::move(prepare_state_)));
  auto* data = allocator_.UserData<TServerSharedData>();
  data_.Set(data);
  RETURN_NOT_OK(data->AllocatorsInitialized(allocator_));
  SetReady();
  return Status::OK();
}

Status SharedMemoryManager::InitializeChildAllocatorsAndObjects(std::string_view uuid) {
  if (ready_) {
    return Status::OK();
  }
  RETURN_NOT_OK(allocator_.InitChild(address_segment_, VERIFY_RESULT(MakeAllocatorName(uuid))));
  data_.Set(allocator_.UserData<TServerSharedData>());
  SetReady();
  return Status::OK();
}

void SharedMemoryManager::SetReady() {
  ready_.store(true);

  decltype(ready_callback_) callback;
  {
    std::lock_guard lock(mutex_);
    callback.swap(ready_callback_);
  }

  if (callback) {
    callback();
  }
}

SharedExchange::SharedExchange(SharedExchangeHeader& header, size_t exchange_size)
    : header_(header), exchange_size_(exchange_size) {}

std::byte* SharedExchange::Obtain(size_t required_size) {
  last_size_ = required_size;
  required_size += header_.header_size();
  if (required_size > exchange_size_) {
    return nullptr;
  }
  return header_.data();
}

Status SharedExchange::SendRequest() {
  return header_.SendRequest(failed_previous_request_, last_size_);
}

bool SharedExchange::ResponseReady() {
  return header_.ResponseReady();
}

Result<Slice> SharedExchange::FetchResponse(CoarseTimePoint deadline) {
  auto size_res = header_.FetchResponse(ToSystem(deadline));
  if (!size_res.ok()) {
    failed_previous_request_ = true;
    return size_res.status();
  }
  failed_previous_request_ = false;
  if (*size_res + header_.header_size() > exchange_size_) {
    return Slice(static_cast<const char*>(nullptr), bit_cast<const char*>(*size_res));
  }
  return Slice(header_.data(), *size_res);
}

bool SharedExchange::ReadyToSend() {
  return header_.ReadyToSend(failed_previous_request_);
}

void SharedExchange::Respond(size_t size) {
  header_.Respond(size);
}

Result<size_t> SharedExchange::Poll() {
  return header_.Poll();
}

void SharedExchange::SignalStop() {
  header_.SignalStop();
}

SharedExchangeRunnable::SharedExchangeRunnable(
    SharedExchange& exchange, uint64_t session_id, const SharedExchangeListener& listener)
    : exchange_(exchange), session_id_(session_id), listener_(listener) {
}

Status SharedExchangeRunnable::Start(YBThreadPool& thread_pool) {
  stop_future_ = stop_promise_.get_future();
  if (!thread_pool.Enqueue(this)) {
    return stop_future_.get();
  }
  return Status::OK();
}

void SharedExchangeRunnable::Done(const Status& status) {
  stop_promise_.set_value(status);
}

void SharedExchangeRunnable::Run() {
  for (;;) {
    auto query_size = exchange_.Poll();
    if (!query_size.ok()) {
      if (!query_size.status().IsShutdownInProgress()) {
        LOG(DFATAL) << "Poll session " << session_id_ <<  " failed: "
                    << query_size.status();
      }
      break;
    }
    listener_(*query_size);
  }
}

SharedExchangeRunnable::~SharedExchangeRunnable() {
  StartShutdown();
  CompleteShutdown();
}

void SharedExchangeRunnable::StartShutdown() {
  if (stop_future_.valid()) {
    exchange_.SignalStop();
  }
}

void SharedExchangeRunnable::CompleteShutdown() {
  if (stop_future_.valid()) {
    stop_future_.get();
  }
}

bool SharedExchangeRunnable::ReadyToShutdown() const {
  return !stop_future_.valid() || stop_future_.wait_for(0s) == std::future_status::ready;
}

class PgSessionSharedMemoryManager::Impl {
 public:
  Impl(std::string_view instance_id, uint64_t session_id, Create create)
      : instance_id_(instance_id),
        session_id_(session_id),
        owner_(create == Create::kTrue) {}

  Status Init() {
    shared_memory_object_ = VERIFY_RESULT(
        MakeSharedMemoryObject(owner_, instance_id_, session_id_));
    mapped_region_ = VERIFY_RESULT(MakeMappedRegion(owner_, shared_memory_object_));
    exchange_.emplace(
        header().exchange_header,
        mapped_region_.get_size() - offsetof(PgSessionSharedHeader, exchange_header));
    return Status::OK();
  }

  ~Impl() {
    if (!owner_ || FLAGS_TEST_skip_remove_tserver_shared_memory_object) {
      return;
    }
    shared_memory_object_.DestroyAndRemove();
  }

  const std::string& instance_id() const {
    return instance_id_;
  }

  uint64_t session_id() const {
    return session_id_;
  }

  SharedExchange& exchange() {
    return *exchange_;
  }

  PgSessionLockOwnerTagShared& object_locking_data() {
    return header().object_locking_data;
  }

 private:
  static Result<InterprocessSharedMemoryObject> MakeSharedMemoryObject(
      bool owner, std::string_view instance_id, uint64_t session_id) {
    auto name = MakeSharedMemoryName(instance_id, session_id);
    if (owner) {
      return InterprocessSharedMemoryObject::Create(
          name, boost::interprocess::mapped_region::get_page_size());
    } else {
      return InterprocessSharedMemoryObject::Open(name);
    }
  }

  static Result<InterprocessMappedRegion> MakeMappedRegion(
      bool owner, const InterprocessSharedMemoryObject& shared_memory_object) {
    auto region = VERIFY_RESULT(shared_memory_object.Map());
    if (owner) {
      new (region.get_address()) PgSessionSharedHeader();
    }
    return region;
  }

  PgSessionSharedHeader& header() {
    return *static_cast<PgSessionSharedHeader*>(mapped_region_.get_address());
  }

  const std::string instance_id_;
  const uint64_t session_id_;
  const bool owner_;
  InterprocessSharedMemoryObject shared_memory_object_;
  InterprocessMappedRegion mapped_region_;
  std::optional<SharedExchange> exchange_;
};

PgSessionSharedMemoryManager::PgSessionSharedMemoryManager() = default;

PgSessionSharedMemoryManager::PgSessionSharedMemoryManager(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PgSessionSharedMemoryManager::PgSessionSharedMemoryManager(
    PgSessionSharedMemoryManager&& rhs) = default;

PgSessionSharedMemoryManager::~PgSessionSharedMemoryManager() = default;

PgSessionSharedMemoryManager& PgSessionSharedMemoryManager::operator=(
    PgSessionSharedMemoryManager&&) = default;

Result<PgSessionSharedMemoryManager> PgSessionSharedMemoryManager::Make(
    const std::string& instance_id, uint64_t session_id, Create create) {
  auto impl = std::make_unique<Impl>(instance_id, session_id, create);
  RETURN_NOT_OK(impl->Init());
  return PgSessionSharedMemoryManager(std::move(impl));
}

Status PgSessionSharedMemoryManager::Cleanup(const std::string& instance_id) {
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

SharedExchange& PgSessionSharedMemoryManager::exchange() {
  return impl_->exchange();
}

PgSessionLockOwnerTagShared& PgSessionSharedMemoryManager::object_locking_data() {
  return impl_->object_locking_data();
}

const std::string& PgSessionSharedMemoryManager::instance_id() const {
  return impl_->instance_id();
}

uint64_t PgSessionSharedMemoryManager::session_id() const {
  return impl_->session_id();
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
