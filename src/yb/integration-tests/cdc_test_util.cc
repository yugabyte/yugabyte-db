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

#include "yb/integration-tests/cdc_test_util.h"

#include <gtest/gtest.h>

#include "yb/consensus/log.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace cdc {

using yb::MiniCluster;

void AssertIntKey(const google::protobuf::RepeatedPtrField<cdc::KeyValuePairPB>& key,
                  int32_t value) {
  ASSERT_EQ(key.size(), 1);
  ASSERT_EQ(key[0].key(), "key");
  ASSERT_EQ(key[0].value().int32_value(), value);
}

void CreateCDCStream(const std::unique_ptr<CDCServiceProxy>& cdc_proxy,
                     const TableId& table_id,
                     CDCStreamId* stream_id) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);

  rpc::RpcController rpc;
  cdc_proxy->CreateCDCStream(req, &resp, &rpc);
  ASSERT_FALSE(resp.has_error());

  if (stream_id) {
    *stream_id = resp.stream_id();
  }
}

Status WaitUntilWalRetentionSecs(std::function<int()> get_wal_retention_secs,
                                 uint32_t expected_wal_retention_secs,
                                 const TableName& table_name) {
  MonoTime start = MonoTime::Now();
  auto timeout = MonoDelta::FromSeconds(20);

  int backoff_exp = 0;
  const int kMaxBackoffExp = 3;
  Status s;
  uint32_t wal_retention_secs;
  while (true) {
    wal_retention_secs = get_wal_retention_secs();
    if (wal_retention_secs == expected_wal_retention_secs) {
      return Status::OK();
    }

    if (MonoTime::Now().GetDeltaSince(start).MoreThan(timeout)) {
      break;
    }

    SleepFor(MonoDelta::FromMilliseconds(1 << backoff_exp));
    backoff_exp = min(backoff_exp + 1, kMaxBackoffExp);
  }
  return STATUS_SUBSTITUTE(TimedOut,
      "wal_retention_secs $0 doesn't match expected $1 for table $2",
      wal_retention_secs, expected_wal_retention_secs, table_name);
}

void VerifyWalRetentionTime(MiniCluster* cluster,
                            const std::string& table_name_start,
                            uint32_t expected_wal_retention_secs) {
  int ntablets_checked = 0;
  for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
    vector<std::shared_ptr<tablet::TabletPeer>> peers;
    mini_tserver->server()->tablet_manager()->GetTabletPeers(&peers);
    for (const auto& peer : peers) {
      const std::string& table_name = peer->tablet_metadata()->table_name();
      if (table_name.substr(0, table_name_start.length()) == table_name_start) {
        auto table_id = peer->tablet_metadata()->table_id();
        ASSERT_OK(WaitUntilWalRetentionSecs([&peer]() { return peer->log()->wal_retention_secs(); },
            expected_wal_retention_secs, table_name));
        ASSERT_OK(WaitUntilWalRetentionSecs(
            [&peer]() { return peer->tablet_metadata()->wal_retention_secs(); },
            expected_wal_retention_secs, table_name));
        ntablets_checked++;
      }
    }
  }
  ASSERT_GT(ntablets_checked, 0);
}

} // namespace cdc
} // namespace yb
