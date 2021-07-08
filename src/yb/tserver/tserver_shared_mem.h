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

#ifndef YB_TSERVER_TSERVER_SHARED_MEM_H
#define YB_TSERVER_TSERVER_SHARED_MEM_H

#include <atomic>

#include "yb/util/shared_mem.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/util/lockfree.h"

namespace yb {
namespace tserver {

class TServerSharedData {
 public:
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

  void SetYSQLCatalogVersion(uint64_t version) {
    catalog_version_.store(version, std::memory_order_release);
  }

  uint64_t ysql_catalog_version() const {
    return catalog_version_.load(std::memory_order_acquire);
  }

  void SetPostgresAuthKey(uint64_t auth_key) {
    postgres_auth_key_ = auth_key;
  }

  uint64_t postgres_auth_key() const {
    return postgres_auth_key_;
  }

 private:
  // Endpoint that should be used by local processes to access this tserver.
  Endpoint endpoint_;
  char host_[255 + 1]; // DNS name max length is 255, but on linux HOST_NAME_MAX is 64.

  std::atomic<uint64_t> catalog_version_{0};
  uint64_t postgres_auth_key_;
};

}  // namespace tserver
}  // namespace yb

#endif // YB_TSERVER_TSERVER_SHARED_MEM_H
