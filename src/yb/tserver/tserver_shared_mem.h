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

#pragma once

#include <atomic>
#include <memory>
#include <string_view>

#include <boost/asio/ip/tcp.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "yb/docdb/object_lock_shared_fwd.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/atomic.h"
#include "yb/util/concurrent_value.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/shmem/shared_mem_allocator.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/thread.h"
#include "yb/util/threadpool.h"
#include "yb/util/uuid.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb {

class ThreadPool;

}

namespace yb::tserver {

class TServerSharedData {
 public:
  static constexpr uint32_t kMaxNumDbCatalogVersions = kYBCMaxNumDbCatalogVersions;

  TServerSharedData();

  ~TServerSharedData();

  // This object is initialized early on, but until address negotiation with postmaster finishes,
  // it is mapped at a temporary address, and pointers in shared memory cannot be used. This is
  // called on tserver when negotiation is finished, and sets up the fields that require pointer
  // support.
  Status AllocatorsInitialized(SharedMemoryBackingAllocator& allocator);

  Status WaitAllocatorsInitialized();

  void SetHostEndpoint(const Endpoint& value, const std::string& host) {
    endpoint_ = value;
    strncpy(host_, host.c_str(), sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = 0;
  }

  const Endpoint& endpoint() const {
    return endpoint_;
  }

  Slice host() const {
    return host_;
  }

  void SetYsqlCatalogVersion(uint64_t version) {
    catalog_version_.store(version, std::memory_order_release);
  }

  uint64_t ysql_catalog_version() const {
    return catalog_version_.load(std::memory_order_acquire);
  }

  void SetYsqlDbCatalogVersion(size_t index, uint64_t version) {
    DCHECK_LT(index, kMaxNumDbCatalogVersions);
    db_catalog_versions_[index].store(version, std::memory_order_release);
  }

  uint64_t ysql_db_catalog_version(size_t index) const {
    DCHECK_LT(index, kMaxNumDbCatalogVersions);
    return db_catalog_versions_[index].load(std::memory_order_acquire);
  }

  void SetCatalogVersionTableInPerdbMode(bool perdb_mode) {
    catalog_version_table_in_perdb_mode_.store(perdb_mode, std::memory_order_release);
  }

  std::optional<bool> catalog_version_table_in_perdb_mode() const {
    return catalog_version_table_in_perdb_mode_.load(std::memory_order_acquire);
  }

  void SetPostgresAuthKey(uint64_t auth_key) {
    postgres_auth_key_ = auth_key;
  }

  uint64_t postgres_auth_key() const {
    return postgres_auth_key_;
  }

  void SetTserverUuid(const std::string& tserver_uuid) {
    DCHECK_EQ(tserver_uuid.size(), 32);
    a2b_hex(tserver_uuid.c_str(), tserver_uuid_, 16);
  }

  const unsigned char* tserver_uuid() const {
    return tserver_uuid_;
  }

  void SetCronLeaderLease(MonoTime cron_leader_lease_end) {
    cron_leader_lease_ = cron_leader_lease_end;
  }

  bool IsCronLeader() const;

  void SetPid(pid_t pid) {
    pid_ = pid;
  }

  pid_t pid() const {
    return pid_;
  }

  docdb::ObjectLockSharedState* object_lock_state() const {
    return object_lock_state_.get();
  }

 private:
  // Endpoint that should be used by local processes to access this tserver.
  Endpoint endpoint_;
  char host_[255 + 1]; // DNS name max length is 255, but on linux HOST_NAME_MAX is 64.

  std::atomic<uint64_t> catalog_version_{0};
  uint64_t postgres_auth_key_;
  unsigned char tserver_uuid_[16]; // Tserver UUID is stored as raw bytes.

  std::atomic<uint64_t> db_catalog_versions_[kMaxNumDbCatalogVersions] = {0};
  // See same variable comments in CatalogManager.
  std::atomic<std::optional<bool>> catalog_version_table_in_perdb_mode_{std::nullopt};

  std::atomic<MonoTime> cron_leader_lease_{MonoTime::kUninitialized};

  // pid of the local TServer
  pid_t pid_;

  // Whether AllocatorsInitialized() has been called -- until then, pointers in shared memory cannot
  // be used.
  std::atomic<bool> fully_initialized_{false};

  SharedMemoryUniquePtr<docdb::ObjectLockSharedState> object_lock_state_;
};

using SharedMemoryReadyCallback = std::function<void()>;

class SharedMemoryManager {
 public:
  ~SharedMemoryManager();

  Status InitializeTServer(std::string_view uuid);
  Status PrepareNegotiationTServer();
  Status SkipNegotiation();

  Status InitializePostmaster(int fd);
  Status InitializePgBackend(std::string_view uuid);

  Status ShutdownNegotiator();

  int NegotiationFd() const;

  bool IsReady() const {
    return ready_;
  }

  void SetReadyCallback(SharedMemoryReadyCallback&& callback);

  auto SharedData() {
    return data_.get();
  }

 private:
  void ExecuteParentNegotiator(const std::shared_ptr<AddressSegmentNegotiator>& negotiator);

  Status PrepareAllocators(std::string_view uuid);
  Status InitializeParentAllocatorsAndObjects();
  Status InitializeChildAllocatorsAndObjects(std::string_view uuid);

  void SetReady();

  ReservedAddressSegment address_segment_;
  SharedMemoryBackingAllocator allocator_;

  ThreadPtr parent_negotiator_thread_;
  std::weak_ptr<AddressSegmentNegotiator> parent_negotiator_;
  bool is_parent_ = false;

  mutable std::mutex mutex_;
  SharedMemoryReadyCallback ready_callback_ GUARDED_BY(mutex_);

  std::atomic<bool> ready_{false};
  SharedMemoryAllocatorPrepareState prepare_state_;
  ConcurrentPointer<TServerSharedData> data_{nullptr};
};

using PgSessionLockOwnerTagShared = ChildProcessRO<docdb::SessionLockOwnerTag>;

YB_STRONGLY_TYPED_BOOL(Create);

class SharedExchangeHeader;

class SharedExchange {
 public:
  SharedExchange(SharedExchangeHeader& header, size_t exchange_size);

  std::byte* Obtain(size_t required_size);
  Status SendRequest();
  Result<Slice> FetchResponse(CoarseTimePoint deadline);
  bool ResponseReady();
  bool ReadyToSend();
  void Respond(size_t size);
  Result<size_t> Poll();
  void SignalStop();

 private:
  SharedExchangeHeader& header_;
  size_t exchange_size_;
  size_t last_size_;
  bool failed_previous_request_ = false;
};

using SharedExchangeListener = std::function<void(size_t)>;

class SharedExchangeRunnable :
    public ThreadPoolTask, public std::enable_shared_from_this<SharedExchangeRunnable> {
 public:
  SharedExchangeRunnable(
      SharedExchange& exchange, uint64_t session_id, const SharedExchangeListener& listener);

  ~SharedExchangeRunnable();

  SharedExchange& exchange() {
    return exchange_;
  }

  bool ReadyToShutdown() const;

  Status Start(YBThreadPool& thread_pool);

  void StartShutdown();
  void CompleteShutdown();

 private:
  void Run() override;
  void Done(const Status& status) override;

  SharedExchange& exchange_;
  uint64_t session_id_;
  SharedExchangeListener listener_;
  std::promise<Status> stop_promise_;
  std::shared_future<Status> stop_future_;
};

struct SharedExchangeMessage {
  uint64_t session_id;
  size_t size;
};

class PgSessionSharedMemoryManager {
 public:
  PgSessionSharedMemoryManager();
  PgSessionSharedMemoryManager(PgSessionSharedMemoryManager&&);
  ~PgSessionSharedMemoryManager();

  PgSessionSharedMemoryManager& operator=(PgSessionSharedMemoryManager&&);

  const std::string& instance_id() const;
  uint64_t session_id() const;

  SharedExchange& exchange();

  [[nodiscard]] PgSessionLockOwnerTagShared& object_locking_data();

  static Result<PgSessionSharedMemoryManager> Make(
      const std::string& instance_id, uint64_t session_id, Create create);

  static Status Cleanup(const std::string& instance_id);

 private:
  class Impl;

  explicit PgSessionSharedMemoryManager(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

constexpr size_t kTooBigResponseMask = 1ULL << 63;
constexpr size_t kBigSharedMemoryMask = 1ULL << 62;
constexpr size_t kBigSharedMemoryIdShift = 40;

std::string MakeSharedMemoryBigSegmentName(const std::string& instance_id, uint64_t id);

}  // namespace yb::tserver
