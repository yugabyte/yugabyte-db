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

#include "yb/master/master_ysql_lease.proxy.h"

namespace yb::master {

// Wrapper class around RPCs to the MasterYsqlLease service. All RPC wrapper methods check the error
// field in response objects. For now only used in tests.
class MasterYsqlLeaseClient {
 public:
  explicit MasterYsqlLeaseClient(MasterYsqlLeaseProxy&& proxy) noexcept;

  Result<RefreshYsqlLeaseInfoPB> RefreshYsqlLease(
      const std::string& permanent_uuid, int64_t instance_seqno, uint64_t time_ms,
      std::optional<uint64_t> current_lease_epoch);

  Status RelinquishYsqlLease(const std::string& permanent_uuid, int64_t instance_seqno);

 private:
  MasterYsqlLeaseProxy proxy_;
};

}  // namespace yb::master
