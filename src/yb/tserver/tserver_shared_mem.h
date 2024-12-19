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

#pragma once

#include <atomic>
#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "yb/gutil/strings/escaping.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/atomic.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_fwd.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/thread.h"
#include "yb/util/uuid.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::tserver {

class TServerSharedData {
 public:
  static constexpr uint32_t kMaxNumDbCatalogVersions = kYBCMaxNumDbCatalogVersions;

  TServerSharedData() {
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
};

YB_STRONGLY_TYPED_BOOL(Create);

class SharedExchange {
 public:
  SharedExchange(SharedExchange&&);
  ~SharedExchange();

  std::byte* Obtain(size_t required_size);
  Status SendRequest();
  Result<Slice> FetchResponse(CoarseTimePoint deadline);
  bool ResponseReady() const;
  bool ReadyToSend() const;
  void Respond(size_t size);
  Result<size_t> Poll();
  void SignalStop();

  const std::string& instance_id() const;
  uint64_t session_id() const;

  static Status Cleanup(const std::string& instance_id);

  static Result<SharedExchange> Make(
      const std::string& instance_id, uint64_t session_id, Create create);

 private:
  class Impl;

  explicit SharedExchange(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

using SharedExchangeListener = std::function<void(size_t)>;

class SharedExchangeThread {
 public:
  SharedExchangeThread(SharedExchange exchange, const SharedExchangeListener& listener);

  ~SharedExchangeThread();

  SharedExchange& exchange() {
    return exchange_;
  }

  bool ReadyToShutdown() const {
    return ready_to_complete_.load();
  }

  void StartShutdown();
  void CompleteShutdown();

 private:
  SharedExchange exchange_;
  ThreadPtr thread_;
  std::atomic<bool> ready_to_complete_{false};
};

struct SharedExchangeMessage {
  uint64_t session_id;
  size_t size;
};

constexpr size_t kTooBigResponseMask = 1ULL << 63;
constexpr size_t kBigSharedMemoryMask = 1ULL << 62;
constexpr size_t kBigSharedMemoryIdShift = 40;

std::string MakeSharedMemoryBigSegmentName(const std::string& instance_id, uint64_t id);

}  // namespace yb::tserver
