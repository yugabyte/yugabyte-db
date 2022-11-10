// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

namespace yb {

class QLWriteRequestPB;
class TimeSeries;

namespace client {
class YBTableName;
}

namespace consensus {
class ConsensusServiceProxy;
}

namespace server {
class GenericServiceProxy;
}

namespace tablet {
class TabletPeer;
}

namespace tserver {

class MiniTabletServer;
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;

class TabletServerTestBase : public YBTest {
 public:
  typedef std::pair<int32_t, int32_t> KeyValue;

  explicit TabletServerTestBase(TableType table_type = YQL_TABLE_TYPE);
  ~TabletServerTestBase();

  // Starts the tablet server, override to start it later.
  void SetUp() override;
  void TearDown() override;

  virtual void StartTabletServer();

  Status WaitForTabletRunning(const char *tablet_id);

  void UpdateTestRowRemote(int tid,
                           int32_t row_idx,
                           int32_t new_val,
                           TimeSeries *ts = nullptr);

  void ResetClientProxies();

  // Inserts 'num_rows' test rows directly into the tablet (i.e not via RPC)
  void InsertTestRowsDirect(int32_t start_row, int32_t num_rows);

  // Inserts 'num_rows' test rows remotely into the tablet (i.e via RPC)
  // Rows are grouped in batches of 'count'/'num_batches' size.
  // Batch size defaults to 1.
  void InsertTestRowsRemote(int tid,
                            int32_t first_row,
                            int32_t count,
                            int32_t num_batches = -1,
                            TabletServerServiceProxy* proxy = nullptr,
                            std::string tablet_id = kTabletId,
                            std::vector<uint64_t>* write_hybrid_times_collector = nullptr,
                            TimeSeries *ts = nullptr,
                            bool string_field_defined = true);

  // Delete specified test row range.
  void DeleteTestRowsRemote(int32_t first_row,
                            int32_t count,
                            TabletServerServiceProxy* proxy = nullptr,
                            std::string tablet_id = kTabletId);

  void BuildTestRow(int index, QLWriteRequestPB* req);

  void ShutdownTablet();

  Status ShutdownAndRebuildTablet();

  // Verifies that a set of expected rows (key, value) is present in the tablet.
  void VerifyRows(const Schema& schema, const std::vector<KeyValue>& expected);

 protected:
  static const client::YBTableName kTableName;
  static const char* kTabletId;

  const Schema schema_;
  Schema key_schema_;
  TableType table_type_;

  std::unique_ptr<rpc::Messenger> client_messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;

  std::unique_ptr<MiniTabletServer> mini_server_;
  std::shared_ptr<tablet::TabletPeer> tablet_peer_;
  std::unique_ptr<TabletServerServiceProxy> proxy_;
  std::unique_ptr<TabletServerAdminServiceProxy> admin_proxy_;
  std::unique_ptr<consensus::ConsensusServiceProxy> consensus_proxy_;
  std::unique_ptr<server::GenericServiceProxy> generic_proxy_;


  MetricRegistry ts_test_metric_registry_;
  scoped_refptr<MetricEntity> ts_test_metric_entity_;

  void* shared_region_;
};

} // namespace tserver
} // namespace yb
