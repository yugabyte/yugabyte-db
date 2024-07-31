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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/master/master_fwd.h"


namespace yb {
namespace pgwrapper {

using PartitionKeyTabletMap = std::map<std::string, master::TabletInfoPtr>;
using VerifyTabletsCallback = std::function<Status(const PartitionKeyTabletMap&)>;
using SelectTabletCallback =
    std::function<Result<master::TabletInfoPtr>(const PartitionKeyTabletMap& tablets)>;

struct SelectFirstTabletPolicy {
  Result<master::TabletInfoPtr> operator()(const PartitionKeyTabletMap& tablets);
};

struct SelectLastTabletPolicy {
  Result<master::TabletInfoPtr> operator()(const PartitionKeyTabletMap& tablets);
};

struct SelectMiddleTabletPolicy {
  Result<master::TabletInfoPtr> operator()(const PartitionKeyTabletMap& tablets);
};

struct TabletSelector {
  const size_t max_selections;
  size_t selections_count;
  SelectTabletCallback policy;
  VerifyTabletsCallback verifier;

  explicit TabletSelector(size_t max_selections,
      SelectTabletCallback selector = SelectFirstTabletPolicy(),
      VerifyTabletsCallback verifier = [](const auto&) { return Status::OK(); });
  Result<master::TabletInfoPtr> operator()(const PartitionKeyTabletMap& tablets);
};

class PgTabletSplitTestBase : public PgMiniTestBase {
 public:
  PgTabletSplitTestBase();
  ~PgTabletSplitTestBase();

 protected:
  void SetUp() override;

  Result<TabletId> GetOnlyTabletId(const TableId& table_id);
  Status SplitTablet(const TabletId& tablet_id);
  Status SplitSingleTablet(const TableId& table_id);
  Status SplitSingleTabletAndWaitForActiveChildTablets(const TableId& table_id);
  Status InvokeSplitTabletRpc(const std::string& tablet_id);
  Status InvokeSplitTabletRpcAndWaitForDataCompacted(const std::string& tablet_id);
  Status InvokeSplitsAndWaitForDataCompacted(
      const TableId& table_id, SelectTabletCallback select_tablet);
  Status DisableCompaction(std::vector<tablet::TabletPeerPtr>* peers);

  Status WaitForSplitCompletion(const TableId& table_id, size_t expected_active_leaders = 2);

  virtual size_t NumTabletServers() override;


  Status DoInvokeSplitTabletRpcAndWaitForDataCompacted(
    const master::TableInfoPtr& table_info, const master::TabletInfoPtr& tablet_info);

  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

Result<PartitionKeyTabletMap> GetTabletsByPartitionKey(const master::TableInfoPtr& table);

} // namespace pgwrapper
} // namespace yb
