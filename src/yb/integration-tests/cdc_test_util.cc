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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/consensus/log.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"
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

Result<xrepl::StreamId> CreateCDCStream(
    const std::unique_ptr<CDCServiceProxy>& cdc_proxy,
    const TableId& table_id,
    cdc::CDCRequestSource source_type) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);
  req.set_source_type(source_type);
  req.set_checkpoint_type(IMPLICIT);

  rpc::RpcController rpc;
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return xrepl::StreamId::FromString(resp.stream_id());
}

void WaitUntilWalRetentionSecs(std::function<int()> get_wal_retention_secs,
                               uint32_t expected_wal_retention_secs,
                               const TableName& table_name) {
  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    uint32_t wal_retention_secs = get_wal_retention_secs();
    if (wal_retention_secs == expected_wal_retention_secs) {
      return true;
    } else {
      LOG(INFO) << "wal_retention_secs " << wal_retention_secs
                << " doesn't match expected " << expected_wal_retention_secs
                << " for table " << table_name;
      return false;
    }
  }, MonoDelta::FromSeconds(20), "Verify wal retention set on Producer."));
}

void VerifyWalRetentionTime(MiniCluster* cluster,
                            const std::string& table_name_start,
                            uint32_t expected_wal_retention_secs) {
  int ntablets_checked = 0;
  for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
    auto peers = mini_tserver->server()->tablet_manager()->GetTabletPeers();
    for (const auto& peer : peers) {
      const std::string& table_name = peer->tablet_metadata()->table_name();
      if (table_name.substr(0, table_name_start.length()) == table_name_start) {
        auto table_id = peer->tablet_metadata()->table_id();
        WaitUntilWalRetentionSecs([&peer]() { return peer->log()->wal_retention_secs(); },
            expected_wal_retention_secs, table_name);
        WaitUntilWalRetentionSecs(
            [&peer]() { return peer->tablet_metadata()->wal_retention_secs(); },
            expected_wal_retention_secs, table_name);
        ntablets_checked++;
      }
    }
  }
  ASSERT_GT(ntablets_checked, 0);
}

size_t NumProducerTabletsPolled(MiniCluster* cluster) {
  size_t size = 0;
  for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
    size_t new_size = 0;
    auto* tserver = mini_tserver->server();
    tserver::XClusterConsumerIf* xcluster_consumer;
    if (tserver && (xcluster_consumer = tserver->GetXClusterConsumer()) &&
        mini_tserver->is_started()) {
      auto tablets_running = xcluster_consumer->TEST_producer_tablets_running();
      new_size = tablets_running.size();
    }
    size += new_size;
  }
  return size;
}

Status CorrectlyPollingAllTablets(
    MiniCluster* cluster, size_t num_producer_tablets, MonoDelta timeout) {
  return LoggedWaitFor(
      [&]() -> Result<bool> {
        static int i = 0;
        constexpr int kNumIterationsWithCorrectResult = 5;
        auto cur_tablets = NumProducerTabletsPolled(cluster);
        if (cur_tablets == num_producer_tablets) {
          if (i++ == kNumIterationsWithCorrectResult) {
            i = 0;
            return true;
          }
        } else {
          i = 0;
        }
        LOG(INFO) << "Tablets being polled: " << cur_tablets;
        return false;
      },
      timeout, "Num producer tablets being polled");
}

} // namespace cdc
} // namespace yb
