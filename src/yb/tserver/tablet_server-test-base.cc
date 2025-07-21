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

#include "yb/tserver/tablet_server-test-base.h"

#include "yb/client/yb_table_name.h"

#include "yb/qlexpr/ql_expr.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.proxy.h"

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/reader_projection.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base.proxy.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_test_util.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"
#include "yb/util/test_graph.h"

using std::string;
using std::vector;

using namespace std::literals;

DEFINE_NON_RUNTIME_int32(rpc_timeout, 1000, "Timeout for RPC calls, in seconds");
DEFINE_NON_RUNTIME_int32(num_updater_threads, 1, "Number of updating threads to launch");
DECLARE_bool(durable_wal_write);
DECLARE_bool(enable_maintenance_manager);
DECLARE_bool(enable_data_block_fsync);
DECLARE_int32(heartbeat_rpc_timeout_ms);
DECLARE_bool(disable_auto_flags_management);
DECLARE_bool(allow_encryption_at_rest);

METRIC_DEFINE_entity(test);

namespace yb {

namespace client {
class YBTableName;
}

namespace tserver {

TabletServerTestBase::TabletServerTestBase(TableType table_type)
    : schema_(GetSimpleTestSchema()),
      table_type_(table_type),
      ts_test_metric_entity_(METRIC_ENTITY_test.Instantiate(
                                 &ts_test_metric_registry_, "ts_server-test")) {
  const_cast<Schema&>(schema_).InitColumnIdsByDefault();

  // Disable the maintenance ops manager since we want to trigger our own
  // maintenance operations at predetermined times.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_maintenance_manager) = false;

  // Decrease heartbeat timeout: we keep re-trying heartbeats when a
  // single master server fails due to a network error. Decreasing
  // the heartbeat timeout to 1 second speeds up unit tests which
  // purposefully specify non-running Master servers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_rpc_timeout_ms) = 1000;

  // Keep unit tests fast, but only if no one has set the flag explicitly.
  if (google::GetCommandLineFlagInfoOrDie("enable_data_block_fsync").is_default) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_data_block_fsync) = false;
  }
}

TabletServerTestBase::~TabletServerTestBase() {}

// Starts the tablet server, override to start it later.
void TabletServerTestBase::SetUp() {
  YBTest::SetUp();

  key_schema_ = schema_.CreateKeyProjection();

  client_messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("Client").Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_messenger_.get());
}

void TabletServerTestBase::TearDown() {
  if (client_messenger_) {
    client_messenger_->Shutdown();
  }

  tablet_peer_.reset();
  if (mini_server_) {
    mini_server_->Shutdown();
  }
}

Result<std::unique_ptr<MiniTabletServer>> TabletServerTestBase::CreateMiniTabletServer() {
  vector<string> drive_dirs;
  for (int i = 0; i < NumDrives(); ++i) {
    drive_dirs.push_back(GetTestPath("TabletServerTest-fsroot") + std::to_string(i));
  }
  return MiniTabletServer::CreateMiniTabletServer(drive_dirs, 0);
}

void TabletServerTestBase::StartTabletServer() {
  // Start server with an invalid master address, so it never successfully
  // heartbeats, even if there happens to be a master running on this machine.

  // Disable AutoFlags management as we dont have a master. AutoFlags will be enabled based on
  // FLAGS_TEST_promote_all_auto_flags in test_main.cc.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_auto_flags_management) = true;

  // Disallow encryption at rest as there is no master.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_encryption_at_rest) = false;

  auto mini_ts = CreateMiniTabletServer();
  CHECK_OK(mini_ts);
  mini_server_ = std::move(*mini_ts);
  auto addr = std::make_shared<server::MasterAddresses>();
  addr->push_back({HostPort("255.255.255.255", 1)});
  mini_server_->options()->SetMasterAddresses(addr);
  CHECK_OK(mini_server_->Start(tserver::WaitTabletsBootstrapped::kFalse));

  // Set up a tablet inside the server.
  CHECK_OK(mini_server_->AddTestTablet(
      kTableName.namespace_name(), kTableName.table_name(), kTabletId, schema_, table_type_));
  tablet_peer_ = CHECK_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  // Creating a tablet is async, we wait here instead of having to handle errors later.
  CHECK_OK(WaitForTabletRunning(kTabletId));

  // Connect to it.
  ResetClientProxies();
}

Status TabletServerTestBase::WaitForTabletRunning(const char *tablet_id) {
  auto* tablet_manager = mini_server_->server()->tablet_manager();
  auto tablet_peer = VERIFY_RESULT(tablet_manager->GetServingTablet(Slice(tablet_id)));

  // Sometimes the disk can be really slow and hence we need a high timeout to wait for consensus.
  RETURN_NOT_OK(tablet_peer->WaitUntilConsensusRunning(MonoDelta::FromSeconds(60)));

  RETURN_NOT_OK(VERIFY_RESULT(tablet_peer->GetConsensus())->EmulateElection());

  return WaitFor([tablet_manager, tablet_peer, tablet_id]() {
        if (tablet_manager->IsTabletInTransition(tablet_id)) {
          return false;
        }
        return tablet_peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
      },
      10s, Format("Complete state transitions for tablet $0", tablet_id));
}

void TabletServerTestBase::UpdateTestRowRemote(int tid,
                                               int32_t row_idx,
                                               int32_t new_val,
                                               TimeSeries *ts) {
  WriteRequestPB req;
  req.set_tablet_id(kTabletId);

  WriteResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_rpc_timeout));
  string new_string_val(strings::Substitute("mutated$0", row_idx));

  AddTestRowUpdate(row_idx, new_val, new_string_val, &req);
  ASSERT_OK(proxy_->Write(req, &resp, &controller));

  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error())<< resp.ShortDebugString();
  ASSERT_EQ(0, resp.per_row_errors_size());
  if (ts) {
    ts->AddValue(1);
  }
}

void TabletServerTestBase::ResetClientProxies() {
  CreateTsClientProxies(HostPort::FromBoundEndpoint(mini_server_->bound_rpc_addr()),
                        proxy_cache_.get(),
                        &proxy_, &admin_proxy_, &consensus_proxy_, &generic_proxy_, &backup_proxy_);
}

// Inserts 'num_rows' test rows directly into the tablet (i.e not via RPC)
void TabletServerTestBase::InsertTestRowsDirect(int32_t start_row, int32_t num_rows) {
  tablet::LocalTabletWriter writer(CHECK_RESULT(tablet_peer_->shared_tablet()));
  QLWriteRequestPB req;
  for (int i = 0; i < num_rows; i++) {
    BuildTestRow(start_row + i, &req);
    CHECK_OK(writer.Write(&req));
  }
}

// Inserts 'num_rows' test rows remotely into the tablet (i.e via RPC)
// Rows are grouped in batches of 'count'/'num_batches' size.
// Batch size defaults to 1.
void TabletServerTestBase::InsertTestRowsRemote(int tid,
                                                int32_t first_row,
                                                int32_t count,
                                                int32_t num_batches,
                                                TabletServerServiceProxy* proxy,
                                                string tablet_id,
                                                vector<uint64_t>* write_hybrid_times_collector,
                                                TimeSeries *ts,
                                                bool string_field_defined) {
  const int kNumRetries = 10;

  if (!proxy) {
    proxy = proxy_.get();
  }

  if (num_batches == -1) {
    num_batches = count;
  }

  WriteRequestPB req;
  req.set_tablet_id(tablet_id);

  WriteResponsePB resp;
  rpc::RpcController controller;

  uint64_t inserted_since_last_report = 0;
  for (int i = 0; i < num_batches; ++i) {
    for (int r = kNumRetries; r-- > 0;) {
      // reset the controller and the request
      controller.Reset();
      controller.set_timeout(MonoDelta::FromSeconds(FLAGS_rpc_timeout));
      req.clear_ql_write_batch();

      auto first_row_in_batch = first_row + (i * count / num_batches);
      auto last_row_in_batch = first_row_in_batch + count / num_batches;

      for (int j = first_row_in_batch; j < last_row_in_batch; j++) {
        if (!string_field_defined) {
          AddTestRowInsert(j, j, &req);
        } else {
          AddTestRowInsert(j, j, strings::Substitute("original$0", j), &req);
        }
      }
      CHECK_OK(DCHECK_NOTNULL(proxy)->Write(req, &resp, &controller));
      if (write_hybrid_times_collector) {
        write_hybrid_times_collector->push_back(resp.propagated_hybrid_time());
      }

      if (!resp.has_error() && resp.per_row_errors_size() == 0) {
        break;
      }

      if (r == 0) {
        LOG(FATAL) << "Failed to insert batch "
                   << first_row_in_batch << "-" << last_row_in_batch
                   << ": " << resp.DebugString();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    inserted_since_last_report += count / num_batches;
    if ((inserted_since_last_report > 100) && ts) {
      ts->AddValue(static_cast<double>(inserted_since_last_report));
      inserted_since_last_report = 0;
    }
  }

  if (ts) {
    ts->AddValue(static_cast<double>(inserted_since_last_report));
  }
}

// Delete specified test row range.
void TabletServerTestBase::DeleteTestRowsRemote(int32_t first_row,
                                                int32_t count,
                                                TabletServerServiceProxy* proxy,
                                                string tablet_id) {
  if (!proxy) {
    proxy = proxy_.get();
  }

  WriteRequestPB req;
  WriteResponsePB resp;
  rpc::RpcController controller;

  req.set_tablet_id(tablet_id);

  for (int32_t rowid = first_row; rowid < first_row + count; rowid++) {
    AddTestRowDelete(rowid, &req);
  }

  SCOPED_TRACE(req.DebugString());
  ASSERT_OK(proxy_->Write(req, &resp, &controller));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
}

void TabletServerTestBase::BuildTestRow(int index, QLWriteRequestPB* req) {
  req->add_hashed_column_values()->mutable_value()->set_int32_value(index);
  auto column_value = req->add_column_values();
  column_value->set_column_id(kFirstColumnId + 1);
  column_value->mutable_expr()->mutable_value()->set_int32_value(index * 2);
  column_value = req->add_column_values();
  column_value->set_column_id(kFirstColumnId + 2);
  column_value->mutable_expr()->mutable_value()->set_string_value(
      StringPrintf("hello %d", index));
}

void TabletServerTestBase::ShutdownTablet() {
  if (mini_server_.get()) {
    // The tablet peer must be destroyed before the TS, otherwise data
    // blocks may be destroyed after their owning block manager.
    tablet_peer_.reset();
    mini_server_->Shutdown();
    mini_server_.reset();
  }
}

Status TabletServerTestBase::ShutdownAndRebuildTablet() {
  ShutdownTablet();

  // Start server.
  auto mini_ts = CreateMiniTabletServer();
  CHECK_OK(mini_ts);
  mini_server_ = std::move(*mini_ts);
  auto addr = std::make_shared<server::MasterAddresses>();
  addr->push_back({HostPort("255.255.255.255", 1)});
  mini_server_->options()->SetMasterAddresses(addr);
  // this should open the tablet created on StartTabletServer()
  RETURN_NOT_OK(mini_server_->Start());

  tablet_peer_ = VERIFY_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  // Connect to it.
  ResetClientProxies();

  // Opening a tablet is async, we wait here instead of having to handle errors later.
  RETURN_NOT_OK(WaitForTabletRunning(kTabletId));
  return Status::OK();
}

void TabletServerTestBase::VerifyRows(
    const Schema& schema, const vector<KeyValue>& expected,
    std::optional<tablet::TabletPeerPtr> tablet_peer) {
  dockv::ReaderProjection projection(schema);
  if (!tablet_peer) {
    tablet_peer = tablet_peer_;
  }
  auto tablet = ASSERT_RESULT((*tablet_peer)->shared_tablet());
  auto iter = tablet->NewRowIterator(projection);
  ASSERT_OK(iter);

  int count = 0;
  qlexpr::QLTableRow row;
  while (ASSERT_RESULT((**iter).FetchNext(&row))) {
    ++count;
  }
  ASSERT_EQ(count, expected.size());
}

const client::YBTableName TabletServerTestBase::kTableName(
    YQL_DATABASE_CQL, "my_keyspace", "test-table");
const char* TabletServerTestBase::kTabletId = "test-tablet";

} // namespace tserver
} // namespace yb
