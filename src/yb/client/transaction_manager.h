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

#pragma once

#include <functional>
#include <memory>

#include "yb/client/client_fwd.h"

#include "yb/common/clock.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.pb.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace client {

using PickStatusTabletCallback = std::function<void(const Result<std::string>&)>;
using UpdateTransactionTablesVersionCallback = std::function<void(const Status&)>;

// TransactionManager manages multiple transactions. It lives at the YQL engine layer.
class TransactionManager {
 public:
  TransactionManager(YBClient* client, const scoped_refptr<ClockBase>& clock,
                     LocalTabletFilter local_tablet_filter);
  ~TransactionManager();

  TransactionManager(TransactionManager&& rhs);
  TransactionManager& operator=(TransactionManager&& rhs);

  // Updates version for list of transaction table and placements, to let
  // TransactionManager decide whether a refresh of cached status tablets is needed.
  void UpdateTransactionTablesVersion(
      uint64_t version,
      UpdateTransactionTablesVersionCallback callback = UpdateTransactionTablesVersionCallback());

  void PickStatusTablet(PickStatusTabletCallback callback, TransactionLocality locality);

  rpc::Rpcs& rpcs();
  YBClient* client() const;

  const scoped_refptr<ClockBase>& clock() const;
  HybridTime Now() const;
  HybridTimeRange NowRange() const;

  void UpdateClock(HybridTime time);

  bool PlacementLocalTransactionsPossible();

  uint64_t GetLoadedStatusTabletsVersion();

  void Shutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace client
} // namespace yb
