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

#include "yb/client/client.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "yb/cdc/cdc_types.h"

#include "yb/client/client_fwd.h"
#include "yb/client/callbacks.h"
#include "yb/client/client-internal.h"
#include "yb/client/client_builder-internal.h"
#include "yb/client/client_utils.h"
#include "yb/client/meta_cache.h"
#include "yb/client/namespace_alterer.h"
#include "yb/client/permissions.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_flags.h"
#include "yb/common/common_util.h"
#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"
#include "yb/dockv/partition.h"
#include "yb/common/pg_types.h"
#include "yb/common/ql_type.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/schema_pbutil.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_dcl.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_encryption.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_error.h"
#include "yb/master/master_util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/outbound_call.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc.h"

#include "yb/tools/yb-admin_util.h"
#include "yb/util/atomic.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/logging_callback.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metric_entity.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/ptree/pt_option.h"

using namespace std::literals;

using google::protobuf::RepeatedField;
using google::protobuf::RepeatedPtrField;
using std::make_pair;
using std::string;
using std::vector;
using yb::master::AddTransactionStatusTabletRequestPB;
using yb::master::AddTransactionStatusTabletResponsePB;
using yb::master::AlterRoleRequestPB;
using yb::master::AlterRoleResponsePB;
using yb::master::CreateCDCStreamRequestPB;
using yb::master::CreateCDCStreamResponsePB;
using yb::master::CreateNamespaceRequestPB;
using yb::master::CreateNamespaceResponsePB;
using yb::master::CreateRoleRequestPB;
using yb::master::CreateRoleResponsePB;
using yb::master::CreateSnapshotRequestPB;
using yb::master::CreateSnapshotResponsePB;
using yb::master::CreateTransactionStatusTableRequestPB;
using yb::master::CreateTransactionStatusTableResponsePB;
using yb::master::CreateUDTypeRequestPB;
using yb::master::CreateUDTypeResponsePB;
using yb::master::DeleteCDCStreamRequestPB;
using yb::master::DeleteCDCStreamResponsePB;
using yb::master::DeleteNamespaceRequestPB;
using yb::master::DeleteNamespaceResponsePB;
using yb::master::DeleteRoleRequestPB;
using yb::master::DeleteRoleResponsePB;
using yb::master::DeleteUDTypeRequestPB;
using yb::master::DeleteUDTypeResponsePB;
using yb::master::GetCDCDBStreamInfoRequestPB;
using yb::master::GetCDCDBStreamInfoResponsePB;
using yb::master::GetCDCStreamRequestPB;
using yb::master::GetCDCStreamResponsePB;
using yb::master::GetIndexBackfillProgressRequestPB;
using yb::master::GetIndexBackfillProgressResponsePB;
using yb::master::GetMasterClusterConfigRequestPB;
using yb::master::GetMasterClusterConfigResponsePB;
using yb::master::GetNamespaceInfoRequestPB;
using yb::master::GetNamespaceInfoResponsePB;
using yb::master::GetPermissionsRequestPB;
using yb::master::GetPermissionsResponsePB;
using yb::master::GetTableLocationsRequestPB;
using yb::master::GetTableLocationsResponsePB;
using yb::master::GetTableSchemaFromSysCatalogRequestPB;
using yb::master::GetTabletLocationsRequestPB;
using yb::master::GetTabletLocationsResponsePB;
using yb::master::GetTransactionStatusTabletsRequestPB;
using yb::master::GetTransactionStatusTabletsResponsePB;
using yb::master::GetUDTypeInfoRequestPB;
using yb::master::GetUDTypeInfoResponsePB;
using yb::master::GetUDTypeMetadataRequestPB;
using yb::master::GetUDTypeMetadataResponsePB;
using yb::master::GetYsqlCatalogConfigRequestPB;
using yb::master::GetYsqlCatalogConfigResponsePB;
using yb::master::GrantRevokePermissionRequestPB;
using yb::master::GrantRevokePermissionResponsePB;
using yb::master::GrantRevokeRoleRequestPB;
using yb::master::GrantRevokeRoleResponsePB;
using yb::master::IsBootstrapRequiredRequestPB;
using yb::master::IsBootstrapRequiredResponsePB;
using yb::master::IsLoadBalancedRequestPB;
using yb::master::IsLoadBalancedResponsePB;
using yb::master::IsLoadBalancerIdleRequestPB;
using yb::master::IsLoadBalancerIdleResponsePB;
using yb::master::IsObjectPartOfXReplRequestPB;
using yb::master::IsObjectPartOfXReplResponsePB;
using yb::master::ListCDCStreamsRequestPB;
using yb::master::ListCDCStreamsResponsePB;
using yb::master::ListLiveTabletServersRequestPB;
using yb::master::ListLiveTabletServersResponsePB;
using yb::master::ListLiveTabletServersResponsePB_Entry;
using yb::master::ListMastersRequestPB;
using yb::master::ListMastersResponsePB;
using yb::master::ListNamespacesRequestPB;
using yb::master::ListNamespacesResponsePB;
using yb::master::ListSnapshotsRequestPB;
using yb::master::ListSnapshotsResponsePB;
using yb::master::ListTablegroupsRequestPB;
using yb::master::ListTablegroupsResponsePB;
using yb::master::ListTablesRequestPB;
using yb::master::ListTablesResponsePB;
using yb::master::ListTablesResponsePB_TableInfo;
using yb::master::ListTabletServersRequestPB;
using yb::master::ListTabletServersResponsePB;
using yb::master::ListTabletServersResponsePB_Entry;
using yb::master::MasterDdlProxy;
using yb::master::MasterReplicationProxy;
using yb::master::PlacementInfoPB;
using yb::master::RedisConfigGetRequestPB;
using yb::master::RedisConfigGetResponsePB;
using yb::master::RedisConfigSetRequestPB;
using yb::master::RedisConfigSetResponsePB;
using yb::master::ReplicationInfoPB;
using yb::master::ReservePgsqlOidsRequestPB;
using yb::master::ReservePgsqlOidsResponsePB;
using yb::master::TabletLocationsPB;
using yb::master::TableIdentifierPB;
using yb::master::UpdateCDCStreamRequestPB;
using yb::master::UpdateCDCStreamResponsePB;
using yb::master::UpdateConsumerOnProducerSplitRequestPB;
using yb::master::UpdateConsumerOnProducerSplitResponsePB;
using yb::master::WaitForYsqlBackendsCatalogVersionRequestPB;
using yb::master::WaitForYsqlBackendsCatalogVersionResponsePB;
using yb::rpc::Messenger;
using yb::tserver::AllowSplitTablet;

using namespace yb::size_literals;  // NOLINT.

namespace {

#ifndef NDEBUG  // debug build has 1h timeout limitation: "Too big timeout specified"
constexpr int kDefaultBackfillIndexClientRpcTimeoutMs = 60 * 60 * 1000;  // 1 hour
#else  // release
constexpr int kDefaultBackfillIndexClientRpcTimeoutMs = 24 * 60 * 60 * 1000;  // 1 day
#endif

}

DEFINE_UNKNOWN_bool(client_suppress_created_logs, false,
            "Suppress 'Created table ...' messages");
TAG_FLAG(client_suppress_created_logs, advanced);
TAG_FLAG(client_suppress_created_logs, hidden);

DEFINE_UNKNOWN_int32(backfill_index_client_rpc_timeout_ms, kDefaultBackfillIndexClientRpcTimeoutMs,
             "Timeout for BackfillIndex RPCs from client to master.");
TAG_FLAG(backfill_index_client_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(ycql_num_tablets, -1,
    "The number of tablets per YCQL table. Default value is -1. "
    "Colocated tables are not affected. "
    "If its value is not set then (1) the value of yb_num_shards_per_tserver is used "
    "in conjunction with the number of tservers to determine the tablet count, (2) in case of "
    "low number of CPU cores (<4) and enable_automatic_tablet_splitting is set to true, "
    "neither the number of tservers nor yb_num_shards_per_tserver are taken into account "
    "to determine the tablet count, the value is determined on base the number of CPU cores only."
    "If the user explicitly specifies a value of the tablet count in the Create Table "
    "DDL statement (with tablets = x syntax) then it takes precedence over the value "
    "of this flag. Needs to be set at tserver.");

DEFINE_RUNTIME_int32(ysql_num_tablets, -1,
    "The number of tablets per YSQL table. Default value is -1. "
    "If its value is not set then (1) the value of ysql_num_shards_per_tserver is used "
    "in conjunction with the number of tservers to determine the tablet count, (2) in case of "
    "low number of CPU cores (<4) and enable_automatic_tablet_splitting is set to true, "
    "neither the number of tservers nor ysql_num_shards_per_tserver are taken into account "
    "to determine the tablet count, the value is determined on base the number of CPU cores only."
    "If the user explicitly specifies a value of the tablet count in the Create Table "
    "DDL statement (split into x tablets syntax) then it takes precedence over the "
    "value of this flag. Needs to be set at tserver.");

// Non-runtime because pggate uses it.
DEFINE_NON_RUNTIME_uint32(wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms,
    20000,
    "WaitForYsqlBackendsCatalogVersion client-to-master RPC timeout. Specifically, both the "
    "postgres-to-tserver and tserver-to-master RPC timeout.");

DEFINE_RUNTIME_uint32(ddl_verification_timeout_multiplier, 5,
    "Multiplier for the timeout used for DDL verification. DDL verification may involve waiting for"
    " DDL operations to finish at the yb-master. This is a multiplier for"
    " default_admin_operation_timeout which is the timeout used for a single DDL operation ");

TAG_FLAG(wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms, advanced);

DEFINE_test_flag(int32, create_namespace_if_not_exist_inject_delay_ms, 0,
                 "After checking a namespace does not exist, inject delay "
                 "before creating the namespace.");

namespace yb {
namespace client {

using internal::MetaCache;
using std::shared_ptr;
using std::pair;

namespace {

void FillFromRepeatedTabletLocations(
    const RepeatedPtrField<TabletLocationsPB>& tablets,
    vector<TabletId>* tablet_uuids,
    vector<string>* ranges,
    vector<TabletLocationsPB>* locations) {
  tablet_uuids->reserve(tablets.size());
  if (ranges) {
    ranges->reserve(tablets.size());
  }
  if (locations) {
    locations->reserve(tablets.size());
  }
  for (const auto& tablet : tablets) {
    if (locations) {
      locations->push_back(tablet);
    }
    tablet_uuids->push_back(tablet.tablet_id());
    if (ranges) {
      const auto& partition = tablet.partition();
      ranges->push_back(partition.ShortDebugString());
    }
  }
}

} // namespace

#define CALL_SYNC_LEADER_MASTER_RPC_EX(service, req, resp, method) \
  do { \
    auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout(); \
    CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(service, req, resp, deadline, method); \
  } while(0);

#define CALL_SYNC_LEADER_MASTER_RPC(req, resp, method) \
  CALL_SYNC_LEADER_MASTER_RPC_EX(Ddl, req, resp, method)

#define CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(service, req, resp, deadline, method) \
  do { \
    RETURN_NOT_OK(data_->SyncLeaderMasterRpc( \
        deadline, req, &resp, BOOST_PP_STRINGIZE(method), \
        &master::BOOST_PP_CAT(BOOST_PP_CAT(Master, service), Proxy)::            \
            BOOST_PP_CAT(method, Async))); \
  } while(0);

// Adapts between the internal LogSeverity and the client's YBLogSeverity.
static void LoggingAdapterCB(YBLoggingCallback* user_cb,
                             LogSeverity severity,
                             const char* filename,
                             int line_number,
                             const struct ::tm* time,
                             const char* message,
                             size_t message_len) {
  YBLogSeverity client_severity;
  switch (severity) {
    case yb::SEVERITY_INFO:
      client_severity = SEVERITY_INFO;
      break;
    case yb::SEVERITY_WARNING:
      client_severity = SEVERITY_WARNING;
      break;
    case yb::SEVERITY_ERROR:
      client_severity = SEVERITY_ERROR;
      break;
    case yb::SEVERITY_FATAL:
      client_severity = SEVERITY_FATAL;
      break;
    default:
      LOG(FATAL) << "Unknown YB log severity: " << severity;
  }
  user_cb->Run(client_severity, filename, line_number, time,
               message, message_len);
}

void InitLogging() {
  InitGoogleLoggingSafeBasic("yb_client");
}

void InstallLoggingCallback(YBLoggingCallback* cb) {
  RegisterLoggingCallback(Bind(&LoggingAdapterCB, Unretained(cb)));
}

void UninstallLoggingCallback() {
  UnregisterLoggingCallback();
}

void SetVerboseLogLevel(int level) {
  FLAGS_v = level;
}

Status SetInternalSignalNumber(int signum) {
  return SetStackTraceSignal(signum);
}

YBClientBuilder::YBClientBuilder()
  : data_(new YBClientBuilder::Data()) {
}

YBClientBuilder::~YBClientBuilder() {
}

YBClientBuilder& YBClientBuilder::clear_master_server_addrs() {
  data_->master_server_addrs_.clear();
  return *this;
}

YBClientBuilder& YBClientBuilder::master_server_addrs(const vector<string>& addrs) {
  for (const string& addr : addrs) {
    data_->master_server_addrs_.push_back(addr);
  }
  return *this;
}

YBClientBuilder& YBClientBuilder::add_master_server_addr(const string& addr) {
  data_->master_server_addrs_.push_back(addr);
  return *this;
}

YBClientBuilder& YBClientBuilder::skip_master_flagfile(bool should_skip) {
  data_->skip_master_flagfile_ = should_skip;
  return *this;
}

YBClientBuilder& YBClientBuilder::wait_for_leader_election_on_init(bool should_wait) {
  data_->wait_for_leader_election_on_init_ = should_wait;
  return *this;
}

YBClientBuilder& YBClientBuilder::default_admin_operation_timeout(const MonoDelta& timeout) {
  data_->default_admin_operation_timeout_ = timeout;
  return *this;
}

YBClientBuilder& YBClientBuilder::default_rpc_timeout(const MonoDelta& timeout) {
  data_->default_rpc_timeout_ = timeout;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_num_reactors(int32_t num_reactors) {
  CHECK_GT(num_reactors, 0);
  data_->num_reactors_ = num_reactors;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_cloud_info_pb(const CloudInfoPB& cloud_info_pb) {
  data_->cloud_info_pb_ = cloud_info_pb;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_metric_entity(
    const scoped_refptr<MetricEntity>& metric_entity) {
  data_->metric_entity_ = metric_entity;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_client_name(const std::string& name) {
  data_->client_name_ = name;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_callback_threadpool_size(size_t size) {
  data_->threadpool_size_ = size;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_tserver_uuid(const TabletServerId& uuid) {
  data_->uuid_ = uuid;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_parent_mem_tracker(const MemTrackerPtr& mem_tracker) {
  data_->parent_mem_tracker_ = mem_tracker;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_master_address_flag_name(const std::string& value) {
  data_->master_address_flag_name_ = value;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_skip_master_leader_resolution(bool value) {
  data_->skip_master_leader_resolution_ = value;
  return *this;
}

YBClientBuilder& YBClientBuilder::AddMasterAddressSource(const MasterAddressSource& source) {
  data_->master_address_sources_.push_back(source);
  return *this;
}

Status YBClientBuilder::DoBuild(rpc::Messenger* messenger,
                                server::ClockPtr clock,
                                std::unique_ptr<YBClient>* client) {
  RETURN_NOT_OK(CheckCPUFlags());

  std::unique_ptr<YBClient> c(new YBClient());
  c->data_->client_name_ = data_->client_name_ + "_" + c->data_->id_.ToString();

  // Init messenger.
  if (messenger) {
    c->data_->messenger_holder_ = nullptr;
    c->data_->messenger_ = messenger;
  } else {
    c->data_->messenger_holder_ = VERIFY_RESULT(client::CreateClientMessenger(
        data_->client_name_, data_->num_reactors_,
        data_->metric_entity_, data_->parent_mem_tracker_));
    c->data_->messenger_ = c->data_->messenger_holder_.get();
  }
  c->data_->proxy_cache_ = std::make_unique<rpc::ProxyCache>(c->data_->messenger_);
  c->data_->metric_entity_ = data_->metric_entity_;

  c->data_->master_address_flag_name_ = data_->master_address_flag_name_;
  c->data_->master_address_sources_ = data_->master_address_sources_;
  c->data_->master_server_addrs_ = data_->master_server_addrs_;
  c->data_->skip_master_flagfile_ = data_->skip_master_flagfile_;
  c->data_->default_admin_operation_timeout_ = data_->default_admin_operation_timeout_;
  c->data_->default_rpc_timeout_ = data_->default_rpc_timeout_;
  c->data_->wait_for_leader_election_on_init_ = data_->wait_for_leader_election_on_init_;

  auto callback_threadpool_size = data_->threadpool_size_;
  if (callback_threadpool_size == YBClientBuilder::Data::kUseNumReactorsAsNumThreads) {
    callback_threadpool_size = c->data_->messenger_->num_reactors();
  }
  c->data_->use_threadpool_for_callbacks_ = callback_threadpool_size != 0;
  if (callback_threadpool_size == 0) {
    callback_threadpool_size = 1;
  }

  // Not using an underscore because we sometimes get shortened thread names like "master_c" and it
  // is clearer to see "mastercb" instead.
  ThreadPoolBuilder tpb(data_->client_name_ + "cb");
  tpb.set_max_threads(narrow_cast<int>(callback_threadpool_size));
  tpb.set_min_threads(1);
  std::unique_ptr<ThreadPool> tp;
  RETURN_NOT_OK_PREPEND(
      tpb.Build(&tp),
      Format("Could not create callback threadpool with $0 max threads",
             callback_threadpool_size));
  c->data_->threadpool_ = std::move(tp);

  // Let's allow for plenty of time for discovering the master the first
  // time around.
  auto deadline = CoarseMonoClock::Now() + c->default_admin_operation_timeout();
  for (;;) {
    auto status = c->data_->SetMasterServerProxy(deadline,
            data_->skip_master_leader_resolution_,
            data_->wait_for_leader_election_on_init_);
    if (status.ok()) {
      break;
    }
    if (!status.IsNotFound() || CoarseMonoClock::Now() >= deadline) {
      RETURN_NOT_OK_PREPEND(status, "Could not locate the leader master")
    }
  }

  c->data_->meta_cache_.reset(new MetaCache(c.get()));

  c->data_->clock_ = clock;

  c->data_->cloud_info_pb_ = data_->cloud_info_pb_;
  c->data_->uuid_ = data_->uuid_;

  client->swap(c);
  return Status::OK();
}

Result<std::unique_ptr<YBClient>> YBClientBuilder::Build(
    rpc::Messenger* messenger, const server::ClockPtr& clock) {
  std::unique_ptr<YBClient> client;
  RETURN_NOT_OK(DoBuild(messenger, clock, &client));
  return client;
}

Result<std::unique_ptr<YBClient>> YBClientBuilder::Build(
    std::unique_ptr<rpc::Messenger>&& messenger, const server::ClockPtr& clock) {
  std::unique_ptr<YBClient> client;
  auto ok = false;
  auto scope_exit = ScopeExit([&ok, &messenger] {
    if (!ok) {
      messenger->Shutdown();
    }
  });
  RETURN_NOT_OK(DoBuild(messenger.get(), clock, &client));
  ok = true;
  client->data_->messenger_holder_ = std::move(messenger);
  return client;
}

YBClient::YBClient() : data_(new YBClient::Data()) {
  yb::InitCommonFlags();
}

YBClient::~YBClient() {
  Shutdown();
}

void YBClient::Shutdown() {
  data_->StartShutdown();
  if (data_->messenger_holder_) {
    data_->messenger_holder_->Shutdown();
  }
  if (data_->threadpool_) {
    data_->threadpool_->Shutdown();
  }
  data_->CompleteShutdown();
}

std::unique_ptr<YBTableCreator> YBClient::NewTableCreator() {
  return std::unique_ptr<YBTableCreator>(new YBTableCreator(this));
}

Status YBClient::IsCreateTableInProgress(const YBTableName& table_name,
                                         bool *create_in_progress) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->IsCreateTableInProgress(this, table_name, "" /* table_id */, deadline,
                                        create_in_progress);
}

Status YBClient::WaitForCreateTableToFinish(const YBTableName& table_name) {
  const auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return WaitForCreateTableToFinish(table_name, deadline);
}

Status YBClient::WaitForCreateTableToFinish(
    const YBTableName& table_name, const CoarseTimePoint& deadline) {
  return data_->WaitForCreateTableToFinish(this, table_name, "" /* table_id */, deadline);
}

Status YBClient::WaitForCreateTableToFinish(const string& table_id) {
  const auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return WaitForCreateTableToFinish(table_id, deadline);
}

Status YBClient::WaitForCreateTableToFinish(
    const string& table_id, const CoarseTimePoint& deadline) {
  const YBTableName empty_table_name;
  return data_->WaitForCreateTableToFinish(this, empty_table_name, table_id, deadline);
}

Status YBClient::WaitForDeleteTableToFinish(const string& table_id) {
  const auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return WaitForDeleteTableToFinish(table_id, deadline);
}

Status YBClient::WaitForDeleteTableToFinish(
    const string& table_id, const CoarseTimePoint& deadline) {
  return data_->WaitForDeleteTableToFinish(this, table_id, deadline);
}

Status YBClient::TruncateTable(const string& table_id, bool wait) {
  return TruncateTables({table_id}, wait);
}

Status YBClient::TruncateTables(const vector<string>& table_ids, bool wait) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->TruncateTables(this, table_ids, deadline, wait);
}

Status YBClient::BackfillIndex(const TableId& table_id, bool wait, CoarseTimePoint deadline) {
  if (deadline == CoarseTimePoint()) {
    deadline = CoarseMonoClock::Now() + FLAGS_backfill_index_client_rpc_timeout_ms * 1ms;
  }
  return data_->BackfillIndex(this, YBTableName(), table_id, deadline, wait);
}

Status YBClient::GetIndexBackfillProgress(
    const std::vector<TableId>& index_ids,
    RepeatedField<google::protobuf::uint64>* rows_processed_entries) {
  GetIndexBackfillProgressRequestPB req;
  GetIndexBackfillProgressResponsePB resp;
  for (auto &index_id : index_ids) {
    req.add_index_ids(index_id);
  }
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetIndexBackfillProgress);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  *rows_processed_entries = std::move(resp.rows_processed_entries());
  return Status::OK();
}

Result<master::GetBackfillStatusResponsePB> YBClient::GetBackfillStatus(
    const std::vector<std::string_view>& table_ids) {
  const auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetBackfillStatus(table_ids, deadline);
}

Status YBClient::DeleteTable(const YBTableName& table_name, bool wait) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->DeleteTable(this,
                            table_name,
                            "" /* table_id */,
                            false /* is_index_table */,
                            deadline,
                            nullptr /* indexed_table_name */,
                            wait);
}

Status YBClient::DeleteTable(const string& table_id,
                             bool wait,
                             const TransactionMetadata *txn,
                             CoarseTimePoint deadline) {
  return data_->DeleteTable(this,
                            YBTableName(),
                            table_id,
                            false /* is_index_table */,
                            PatchAdminDeadline(deadline),
                            nullptr /* indexed_table_name */,
                            wait,
                            txn);
}

Status YBClient::DeleteIndexTable(const YBTableName& table_name,
                                  YBTableName* indexed_table_name,
                                  bool wait,
                                  const TransactionMetadata *txn) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->DeleteTable(this,
                            table_name,
                            "" /* table_id */,
                            true /* is_index_table */,
                            deadline,
                            indexed_table_name,
                            wait,
                            txn);
}

Status YBClient::DeleteIndexTable(const string& table_id,
                                  YBTableName* indexed_table_name,
                                  bool wait,
                                  const TransactionMetadata *txn,
                                  CoarseTimePoint deadline) {
  return data_->DeleteTable(this,
                            YBTableName(),
                            table_id,
                            true /* is_index_table */,
                            PatchAdminDeadline(deadline),
                            indexed_table_name,
                            wait,
                            txn);
}

Status YBClient::FlushTables(const std::vector<TableId>& table_ids,
                             bool add_indexes,
                             int timeout_secs,
                             bool is_compaction) {
  auto deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(timeout_secs);
  return data_->FlushTables(this,
                            table_ids,
                            add_indexes,
                            deadline,
                            is_compaction);
}

Status YBClient::FlushTables(const std::vector<YBTableName>& table_names,
                             bool add_indexes,
                             int timeout_secs,
                             bool is_compaction) {
  auto deadline = CoarseMonoClock::Now() + MonoDelta::FromSeconds(timeout_secs);
  return data_->FlushTables(this,
                            table_names,
                            add_indexes,
                            deadline,
                            is_compaction);
}

Result<TableCompactionStatus> YBClient::GetCompactionStatus(
    const YBTableName& table_name, bool show_tablets) {
  return data_->GetCompactionStatus(
      table_name, show_tablets, CoarseMonoClock::Now() + default_admin_operation_timeout());
}

std::unique_ptr<YBTableAlterer> YBClient::NewTableAlterer(const YBTableName& name) {
  return std::unique_ptr<YBTableAlterer>(new YBTableAlterer(this, name));
}

std::unique_ptr<YBTableAlterer> YBClient::NewTableAlterer(const string id) {
  return std::unique_ptr<YBTableAlterer>(new YBTableAlterer(this, id));
}

Status YBClient::IsAlterTableInProgress(const YBTableName& table_name,
                                        const string& table_id,
                                        bool *alter_in_progress) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->IsAlterTableInProgress(this, table_name, table_id, deadline, alter_in_progress);
}

Result<YBTableInfo> YBClient::GetYBTableInfo(const YBTableName& table_name) {
  YBTableInfo info;
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  RETURN_NOT_OK(data_->GetTableSchema(this, table_name, deadline, &info));
  return info;
}

Status YBClient::GetTableSchema(const YBTableName& table_name,
                                YBSchema* schema,
                                dockv::PartitionSchema* partition_schema) {
  Result<YBTableInfo> info = GetYBTableInfo(table_name);
  if (!info.ok()) {
    return info.status();
  }
  // Verify it is not an index table.
  if (info->index_info) {
    return STATUS(NotFound, "The table does not exist");
  }

  *schema = std::move(info->schema);
  *partition_schema = std::move(info->partition_schema);
  return Status::OK();
}

Status YBClient::GetYBTableInfo(const YBTableName& table_name, std::shared_ptr<YBTableInfo> info,
                                StatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetTableSchema(this, table_name, deadline, info, callback);
}


Status YBClient::GetTableSchemaById(const TableId& table_id, std::shared_ptr<YBTableInfo> info,
                                    StatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetTableSchema(this, table_id, deadline, info, callback);
}

Status YBClient::GetTablegroupSchemaById(const TablegroupId& tablegroup_id,
                                         std::shared_ptr<std::vector<YBTableInfo>> info,
                                         StatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetTablegroupSchemaById(this,
                                        tablegroup_id,
                                        deadline,
                                        info,
                                        callback);
}

Status YBClient::GetColocatedTabletSchemaByParentTableId(
    const TableId& parent_colocated_table_id,
    std::shared_ptr<std::vector<YBTableInfo>> info,
    StatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetColocatedTabletSchemaByParentTableId(this,
                                                         parent_colocated_table_id,
                                                         deadline,
                                                         info,
                                                         callback);
}

Result<IndexPermissions> YBClient::GetIndexPermissions(
    const TableId& table_id,
    const TableId& index_id) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->GetIndexPermissions(
      this,
      table_id,
      index_id,
      deadline);
}

Result<IndexPermissions> YBClient::GetIndexPermissions(
    const YBTableName& table_name,
    const YBTableName& index_name) {
  YBTableInfo table_info = VERIFY_RESULT(GetYBTableInfo(table_name));
  YBTableInfo index_info = VERIFY_RESULT(GetYBTableInfo(index_name));
  return GetIndexPermissions(table_info.table_id, index_info.table_id);
}

Result<IndexPermissions> YBClient::WaitUntilIndexPermissionsAtLeast(
    const TableId& table_id,
    const TableId& index_id,
    const IndexPermissions& target_index_permissions,
    const CoarseTimePoint deadline,
    const CoarseDuration max_wait) {
  return data_->WaitUntilIndexPermissionsAtLeast(
      this,
      table_id,
      index_id,
      target_index_permissions,
      deadline,
      max_wait);
}

Result<IndexPermissions> YBClient::WaitUntilIndexPermissionsAtLeast(
    const TableId& table_id,
    const TableId& index_id,
    const IndexPermissions& target_index_permissions,
    const CoarseDuration max_wait) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return WaitUntilIndexPermissionsAtLeast(
      table_id,
      index_id,
      target_index_permissions,
      deadline,
      max_wait);
}

Result<IndexPermissions> YBClient::WaitUntilIndexPermissionsAtLeast(
    const YBTableName& table_name,
    const YBTableName& index_name,
    const IndexPermissions& target_index_permissions,
    const CoarseTimePoint deadline,
    const CoarseDuration max_wait) {
  return data_->WaitUntilIndexPermissionsAtLeast(
      this,
      table_name,
      index_name,
      target_index_permissions,
      deadline,
      max_wait);
}

Result<IndexPermissions> YBClient::WaitUntilIndexPermissionsAtLeast(
    const YBTableName& table_name,
    const YBTableName& index_name,
    const IndexPermissions& target_index_permissions,
    const CoarseDuration max_wait) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_name,
      target_index_permissions,
      deadline,
      max_wait);
}

Status YBClient::CreateNamespace(const std::string& namespace_name,
                                 const boost::optional<YQLDatabase>& database_type,
                                 const std::string& creator_role_name,
                                 const std::string& namespace_id,
                                 const std::string& source_namespace_id,
                                 const boost::optional<uint32_t>& next_pg_oid,
                                 const TransactionMetadata* txn,
                                 const bool colocated,
                                 CoarseTimePoint deadline) {
  CreateNamespaceRequestPB req;
  CreateNamespaceResponsePB resp;
  req.set_name(namespace_name);
  if (!creator_role_name.empty()) {
    req.set_creator_role_name(creator_role_name);
  }
  if (database_type) {
    req.set_database_type(*database_type);
  }
  if (!namespace_id.empty()) {
    req.set_namespace_id(namespace_id);
  }
  if (!source_namespace_id.empty()) {
    req.set_source_namespace_id(source_namespace_id);
  }
  if (next_pg_oid) {
    req.set_next_pg_oid(*next_pg_oid);
  }
  if (txn) {
    txn->ToPB(req.mutable_transaction());
  }
  req.set_colocated(colocated);
  deadline = PatchAdminDeadline(deadline);
  RETURN_NOT_OK(data_->SyncLeaderMasterRpc(
      deadline, req, &resp, "CreateNamespace", &MasterDdlProxy::CreateNamespaceAsync));
  std::string cur_id = resp.has_id() ? resp.id() : namespace_id;

  // Verify that the namespace we found is running so that, once this request returns,
  // the client can send operations without receiving a "namespace not found" error.
  RETURN_NOT_OK(data_->WaitForCreateNamespaceToFinish(
      this, namespace_name, database_type, cur_id, deadline));

  return Status::OK();
}

Status YBClient::CreateNamespaceIfNotExists(const std::string& namespace_name,
                                            const boost::optional<YQLDatabase>& database_type,
                                            const std::string& creator_role_name,
                                            const std::string& namespace_id,
                                            const std::string& source_namespace_id,
                                            const boost::optional<uint32_t>& next_pg_oid,
                                            const bool colocated) {
  bool retried = false;
  while (true) {
    const auto namespace_exists = VERIFY_RESULT(
        !namespace_id.empty() ? NamespaceIdExists(namespace_id)
                              : NamespaceExists(namespace_name));
    if (namespace_exists) {
      // Verify that the namespace we found is running so that, once this request returns,
      // the client can send operations without receiving a "namespace not found" error.
      return data_->WaitForCreateNamespaceToFinish(
          this, namespace_name, database_type, namespace_id,
          CoarseMonoClock::Now() + default_admin_operation_timeout());
    } else if (FLAGS_TEST_create_namespace_if_not_exist_inject_delay_ms > 0) {
      std::this_thread::sleep_for(FLAGS_TEST_create_namespace_if_not_exist_inject_delay_ms * 1ms);
    }

    Status s = CreateNamespace(namespace_name, database_type, creator_role_name, namespace_id,
                               source_namespace_id, next_pg_oid, nullptr /* txn */, colocated);

    // Retain old behavior: return Status::OK() on s.IsAlreadyPresent() error for
    // YQLDatabase::YQL_DATABASE_CQL database_type.
    if (!s.IsAlreadyPresent() || !database_type) {
      return s;
    }
    if (*database_type == YQLDatabase::YQL_DATABASE_CQL) {
      return Status::OK();
    }

    // Do one time retry for YQL_DATABASE_PGSQL.
    if (*database_type == YQLDatabase::YQL_DATABASE_PGSQL && !retried) {
      // Sleep a bit before retrying.
      std::this_thread::sleep_for(1000ms * kTimeMultiplier);
      retried = true;
    } else {
      return s;
    }
  }
  return STATUS(RuntimeError, "Unreachable statement");
}

Status YBClient::IsCreateNamespaceInProgress(const std::string& namespace_name,
                                             const boost::optional<YQLDatabase>& database_type,
                                             const std::string& namespace_id,
                                             bool *create_in_progress) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->IsCreateNamespaceInProgress(this, namespace_name, database_type, namespace_id,
                                            deadline, create_in_progress);
}

Status YBClient::DeleteNamespace(const std::string& namespace_name,
                                 const boost::optional<YQLDatabase>& database_type,
                                 const std::string& namespace_id,
                                 CoarseTimePoint deadline) {
  DeleteNamespaceRequestPB req;
  DeleteNamespaceResponsePB resp;
  req.mutable_namespace_()->set_name(namespace_name);
  if (!namespace_id.empty()) {
    req.mutable_namespace_()->set_id(namespace_id);
  }
  if (database_type) {
    req.set_database_type(*database_type);
    req.mutable_namespace_()->set_database_type(*database_type);
  }
  deadline = PatchAdminDeadline(deadline);
  RETURN_NOT_OK(data_->SyncLeaderMasterRpc(
      deadline, req, &resp, "DeleteNamespace", &MasterDdlProxy::DeleteNamespaceAsync));

  // Verify that, once this request returns, the namespace has been successfully marked as deleted.
  RETURN_NOT_OK(data_->WaitForDeleteNamespaceToFinish(this, namespace_name, database_type,
      namespace_id, CoarseMonoClock::Now() + default_admin_operation_timeout()));

  return Status::OK();
}

Status YBClient::IsDeleteNamespaceInProgress(const std::string& namespace_name,
                                             const boost::optional<YQLDatabase>& database_type,
                                             const std::string& namespace_id,
                                             bool *delete_in_progress) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->IsDeleteNamespaceInProgress(this, namespace_name, database_type, namespace_id,
                                            deadline, delete_in_progress);
}

std::unique_ptr<YBNamespaceAlterer> YBClient::NewNamespaceAlterer(
    const string& namespace_name, const std::string& namespace_id) {
  return std::unique_ptr<YBNamespaceAlterer>(new YBNamespaceAlterer(
      this, namespace_name, namespace_id));
}

Result<vector<NamespaceInfo>> YBClient::ListNamespaces(
  IncludeNonrunningNamespaces include_nonrunning, std::optional<YQLDatabase> database_type) {
  ListNamespacesRequestPB req;
  ListNamespacesResponsePB resp;
  if (include_nonrunning) {
    req.set_include_nonrunning(include_nonrunning);
  }
  if (database_type) {
    req.set_database_type(*database_type);
  }
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListNamespaces);
  auto namespaces = resp.namespaces();
  auto states = resp.states();
  auto colocated = resp.colocated();
  vector<NamespaceInfo> result;
  result.reserve(namespaces.size());
  for (int i = 0; i < namespaces.size(); ++i) {
    NamespaceInfo ns;
    ns.id = namespaces.Get(i);
    ns.state = master::SysNamespaceEntryPB_State(states.Get(i));
    ns.colocated = colocated.Get(i);
    result.push_back(std::move(ns));
  }
  return result;
}

Status YBClient::GetNamespaceInfo(const std::string& namespace_id,
                                  const std::string& namespace_name,
                                  const boost::optional<YQLDatabase>& database_type,
                                  master::GetNamespaceInfoResponsePB* ret) {
  GetNamespaceInfoRequestPB req;
  GetNamespaceInfoResponsePB resp;

  if (!namespace_id.empty()) {
    req.mutable_namespace_()->set_id(namespace_id);
  }
  if (!namespace_name.empty()) {
    req.mutable_namespace_()->set_name(namespace_name);
  }
  if (database_type) {
    req.mutable_namespace_()->set_database_type(*database_type);
  }

  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GetNamespaceInfo);
  ret->Swap(&resp);
  return Status::OK();
}

Status YBClient::ReservePgsqlOids(const std::string& namespace_id,
                                  const uint32_t next_oid, const uint32_t count,
                                  uint32_t* begin_oid, uint32_t* end_oid) {
  ReservePgsqlOidsRequestPB req;
  ReservePgsqlOidsResponsePB resp;
  req.set_namespace_id(namespace_id);
  req.set_next_oid(next_oid);
  req.set_count(count);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, ReservePgsqlOids);
  *begin_oid = resp.begin_oid();
  *end_oid = resp.end_oid();
  return Status::OK();
}

Status YBClient::GetYsqlCatalogMasterVersion(uint64_t *ysql_catalog_version) {
  GetYsqlCatalogConfigRequestPB req;
  GetYsqlCatalogConfigResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetYsqlCatalogConfig);
  *ysql_catalog_version = resp.version();
  return Status::OK();
}

Status YBClient::GrantRevokePermission(GrantRevokeStatementType statement_type,
                                       const PermissionType& permission,
                                       const ResourceType& resource_type,
                                       const std::string& canonical_resource,
                                       const char* resource_name,
                                       const char* namespace_name,
                                       const std::string& role_name) {
  // Setting up request.
  GrantRevokePermissionRequestPB req;
  req.set_role_name(role_name);
  req.set_canonical_resource(canonical_resource);
  if (resource_name != nullptr) {
    req.set_resource_name(resource_name);
  }
  if (namespace_name != nullptr) {
    req.mutable_namespace_()->set_name(namespace_name);
  }
  req.set_resource_type(resource_type);
  req.set_permission(permission);

  req.set_revoke(statement_type == GrantRevokeStatementType::REVOKE);

  GrantRevokePermissionResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, GrantRevokePermission);
  return Status::OK();
}

Result<bool> YBClient::NamespaceExists(
    const std::string& NamespaceName, const std::optional<YQLDatabase>& database_type) {
  for (const auto& ns :
       VERIFY_RESULT(ListNamespaces(IncludeNonrunningNamespaces::kFalse, database_type))) {
    if (ns.id.name() == NamespaceName) {
      return true;
    }
  }
  return false;
}

Result<bool> YBClient::NamespaceIdExists(
    const std::string& NamespaceId, const std::optional<YQLDatabase>& database_type) {
  for (const auto& ns :
       VERIFY_RESULT(ListNamespaces(IncludeNonrunningNamespaces::kFalse, database_type))) {
    if (ns.id.id() == NamespaceId) {
      return true;
    }
  }
  return false;
}

Status YBClient::CreateTablegroup(const std::string& namespace_name,
                                  const std::string& namespace_id,
                                  const std::string& tablegroup_id,
                                  const std::string& tablespace_id,
                                  const TransactionMetadata* txn) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->CreateTablegroup(this,
                                 deadline,
                                 namespace_name,
                                 namespace_id,
                                 tablegroup_id,
                                 tablespace_id,
                                 txn);
}

Status YBClient::DeleteTablegroup(const std::string& tablegroup_id,
                                  const TransactionMetadata* txn) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->DeleteTablegroup(this,
                                 deadline,
                                 tablegroup_id,
                                 txn);
}

Result<vector<master::TablegroupIdentifierPB>>
YBClient::ListTablegroups(const std::string& namespace_name) {
  GetNamespaceInfoResponsePB ret;
  Status s = GetNamespaceInfo("", namespace_name, YQL_DATABASE_PGSQL, &ret);
  if (!s.ok()) {
    return s;
  }

  ListTablegroupsRequestPB req;
  ListTablegroupsResponsePB resp;

  req.set_namespace_id(ret.namespace_().id());
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTablegroups);
  auto* tablegroups = resp.mutable_tablegroups();
  vector<master::TablegroupIdentifierPB> result;
  result.reserve(tablegroups->size());
  for (auto& tg : *tablegroups) {
    result.push_back(std::move(tg));
  }
  return result;
}

Result<bool> YBClient::TablegroupExists(const std::string& namespace_name,
                                        const std::string& tablegroup_id) {
  for (const auto& tg : VERIFY_RESULT(ListTablegroups(namespace_name))) {
    if (tg.id().compare(tablegroup_id) == 0) {
      return true;
    }
  }
  return false;
}

Result<std::shared_ptr<QLType>> YBClient::GetUDType(
    const std::string& namespace_name, const std::string& type_name) {
  // Setting up request.
  GetUDTypeInfoRequestPB req;
  req.mutable_type()->mutable_namespace_()->set_name(namespace_name);
  req.mutable_type()->set_type_name(type_name);

  // Sending request.
  GetUDTypeInfoResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GetUDTypeInfo);

  // Filling in return values.
  auto& udt = *resp.mutable_udtype();
  std::vector<string> field_names;
  field_names.reserve(udt.field_names().size());
  for (auto& field_name : *udt.mutable_field_names()) {
    field_names.push_back(std::move(field_name));
  }

  std::vector<shared_ptr<QLType>> field_types;
  field_types.reserve(udt.field_types().size());
  for (const auto& field_type : udt.field_types()) {
    field_types.push_back(QLType::FromQLTypePB(field_type));
  }

  return QLType::CreateUDType(
      namespace_name, type_name,
      std::move(*udt.mutable_id()), std::move(field_names), std::move(field_types));
}

Status YBClient::CreateRole(const RoleName& role_name,
                            const std::string& salted_hash,
                            const bool login, const bool superuser,
                            const RoleName& creator_role_name) {

  // Setting up request.
  CreateRoleRequestPB req;
  req.set_salted_hash(salted_hash);
  req.set_name(role_name);
  req.set_login(login);
  req.set_superuser(superuser);

  if (!creator_role_name.empty()) {
    req.set_creator_role_name(creator_role_name);
  }

  CreateRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, CreateRole);
  return Status::OK();
}

Status YBClient::AlterRole(const RoleName& role_name,
                           const boost::optional<std::string>& salted_hash,
                           const boost::optional<bool> login,
                           const boost::optional<bool> superuser,
                           const RoleName& current_role_name) {
  // Setting up request.
  AlterRoleRequestPB req;
  req.set_name(role_name);
  if (salted_hash) {
    req.set_salted_hash(*salted_hash);
  }
  if (login) {
    req.set_login(*login);
  }
  if (superuser) {
    req.set_superuser(*superuser);
  }
  req.set_current_role(current_role_name);

  AlterRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, AlterRole);
  return Status::OK();
}

Status YBClient::DeleteRole(const std::string& role_name,
                            const std::string& current_role_name) {
  // Setting up request.
  DeleteRoleRequestPB req;
  req.set_name(role_name);
  req.set_current_role(current_role_name);

  DeleteRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, DeleteRole);
  return Status::OK();
}

static const string kRequirePass = "requirepass";
Status YBClient::SetRedisPasswords(const std::vector<string>& passwords) {
  // TODO: Store hash instead of the password?
  return SetRedisConfig(kRequirePass, passwords);
}

Status YBClient::GetRedisPasswords(vector<string>* passwords) {
  Status s = GetRedisConfig(kRequirePass, passwords);
  if (s.IsNotFound()) {
    // If the redis config has no kRequirePass key.
    passwords->clear();
    s = Status::OK();
  }
  return s;
}

Status YBClient::SetRedisConfig(const string& key, const vector<string>& values) {
  // Setting up request.
  RedisConfigSetRequestPB req;
  req.set_keyword(key);
  for (const auto& value : values) {
    req.add_args(value);
  }
  RedisConfigSetResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, RedisConfigSet);
  return Status::OK();
}

Status YBClient::GetRedisConfig(const string& key, vector<string>* values) {
  // Setting up request.
  RedisConfigGetRequestPB req;
  RedisConfigGetResponsePB resp;
  req.set_keyword(key);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, RedisConfigGet);
  values->clear();
  for (const auto& arg : resp.args())
    values->push_back(arg);
  return Status::OK();
}

Status YBClient::GrantRevokeRole(GrantRevokeStatementType statement_type,
                                 const std::string& granted_role_name,
                                 const std::string& recipient_role_name) {
  // Setting up request.
  GrantRevokeRoleRequestPB req;
  req.set_revoke(statement_type == GrantRevokeStatementType::REVOKE);
  req.set_granted_role(granted_role_name);
  req.set_recipient_role(recipient_role_name);

  GrantRevokeRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, GrantRevokeRole);
  return Status::OK();
}

Status YBClient::GetPermissions(client::internal::PermissionsCache* permissions_cache) {
  if (!permissions_cache) {
    DFATAL_OR_RETURN_NOT_OK(STATUS(InvalidArgument, "Invalid null permissions_cache"));
  }

  boost::optional<uint64_t> version = permissions_cache->version();

  // Setting up request.
  GetPermissionsRequestPB req;
  if (version) {
    req.set_if_version_greater_than(*version);
  }

  GetPermissionsResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Dcl, req, resp, GetPermissions);

  VLOG_WITH_PREFIX(1) << "Got permissions cache: " << resp.ShortDebugString();

  // The first request is a special case. We always replace the cache since we don't have anything.
  if (!version) {
    // We should at least receive cassandra's permissions.
    if (resp.role_permissions_size() == 0) {
      DFATAL_OR_RETURN_NOT_OK(
          STATUS(IllegalState, "Received invalid empty permissions cache from master"));

    }
  } else if (resp.version() == *version) {
      // No roles should have been received if both versions match.
      if (resp.role_permissions_size() != 0) {
        DFATAL_OR_RETURN_NOT_OK(STATUS(IllegalState,
            "Received permissions cache when none was expected because the master's "
            "permissions versions is equal to the client's version"));
      }
      // Nothing to update.
      return Status::OK();
  } else if (resp.version() < *version) {
    // If the versions don't match, then the master's version has to be greater than ours.
    DFATAL_OR_RETURN_NOT_OK(STATUS_SUBSTITUTE(IllegalState,
        "Client's permissions version $0 can't be greater than the master's permissions version $1",
        *version, resp.version()));
  }

  permissions_cache->UpdateRolesPermissions(resp);
  return Status::OK();
}

Status YBClient::CreateUDType(const std::string& namespace_name,
                              const std::string& type_name,
                              const std::vector<std::string>& field_names,
                              const std::vector<std::shared_ptr<QLType>>& field_types) {
  // Setting up request.
  CreateUDTypeRequestPB req;
  req.mutable_namespace_()->set_name(namespace_name);
  req.set_name(type_name);
  for (const string& field_name : field_names) {
    req.add_field_names(field_name);
  }
  for (const std::shared_ptr<QLType>& field_type : field_types) {
    field_type->ToQLTypePB(req.add_field_types());
  }

  CreateUDTypeResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, CreateUDType);
  return Status::OK();
}

Status YBClient::DeleteUDType(const std::string& namespace_name,
                              const std::string& type_name) {
  // Setting up request.
  DeleteUDTypeRequestPB req;
  req.mutable_type()->mutable_namespace_()->set_name(namespace_name);
  req.mutable_type()->set_type_name(type_name);

  DeleteUDTypeResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, DeleteUDType);
  return Status::OK();
}

Result<xrepl::StreamId> YBClient::CreateCDCStream(
    const TableId& table_id,
    const std::unordered_map<std::string, std::string>& options,
    bool active,
    const xrepl::StreamId& db_stream_id) {
  // Setting up request.
  CreateCDCStreamRequestPB req;
  req.set_table_id(table_id);
  if (db_stream_id) {
    req.set_db_stream_id(db_stream_id.ToString());
  }
  req.mutable_options()->Reserve(narrow_cast<int>(options.size()));
  for (const auto& option : options) {
    auto new_option = req.add_options();
    new_option->set_key(option.first);
    new_option->set_value(option.second);
  }
  req.set_initial_state(active ? master::SysCDCStreamEntryPB::ACTIVE
                               : master::SysCDCStreamEntryPB::INITIATED);

  CreateCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, CreateCDCStream);
  return xrepl::StreamId::FromString(resp.stream_id());
}

void YBClient::CreateCDCStream(
    const TableId& table_id,
    const std::unordered_map<std::string, std::string>& options,
    cdc::StreamModeTransactional transactional,
    CreateCDCStreamCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->CreateCDCStream(this, table_id, options, transactional, deadline, callback);
}

Result<xrepl::StreamId> YBClient::CreateCDCSDKStreamForNamespace(
    const NamespaceId& namespace_id,
    const std::unordered_map<std::string, std::string>& options,
    bool populate_namespace_id_as_table_id,
    const ReplicationSlotName& replication_slot_name,
    const std::optional<std::string>& replication_slot_plugin_name,
    const std::optional<CDCSDKSnapshotOption>& consistent_snapshot_option,
    CoarseTimePoint deadline,
    uint64_t *consistent_snapshot_time) {
  CreateCDCStreamRequestPB req;

  if (populate_namespace_id_as_table_id) {
    req.set_table_id(namespace_id);
  } else {
    req.set_namespace_id(namespace_id);
  }

  req.mutable_options()->Reserve(narrow_cast<int>(options.size()));
  for (const auto& option : options) {
    auto new_option = req.add_options();
    new_option->set_key(option.first);
    new_option->set_value(option.second);
  }
  if (!replication_slot_name.empty()) {
    req.set_cdcsdk_ysql_replication_slot_name(replication_slot_name.ToString());
  }
  if (consistent_snapshot_option.has_value()) {
    req.set_cdcsdk_consistent_snapshot_option(*consistent_snapshot_option);
  }
  if (replication_slot_plugin_name.has_value()) {
    req.set_cdcsdk_ysql_replication_slot_plugin_name(*replication_slot_plugin_name);
  }

  CreateCDCStreamResponsePB resp;
  deadline = PatchAdminDeadline(deadline);
  CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(Replication, req, resp, deadline, CreateCDCStream);

  if (consistent_snapshot_time && resp.has_cdcsdk_consistent_snapshot_time()) {
    *consistent_snapshot_time = resp.cdcsdk_consistent_snapshot_time();
  }
  return xrepl::StreamId::FromString(resp.stream_id());
}

Status YBClient::GetCDCStream(
    const xrepl::StreamId& stream_id,
    NamespaceId* ns_id,
    std::vector<ObjectId>* object_ids,
    std::unordered_map<std::string, std::string>* options,
    cdc::StreamModeTransactional* transactional,
    std::optional<uint64_t>* consistent_snapshot_time,
    std::optional<CDCSDKSnapshotOption>* consistent_snapshot_option,
    std::optional<uint64_t>* stream_creation_time,
    std::unordered_map<std::string, PgReplicaIdentity>* replica_identity_map) {

  // Setting up request.
  GetCDCStreamRequestPB req;
  req.set_stream_id(stream_id.ToString());

  // Sending request.
  GetCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetCDCStream);

  // Filling in return values.
  if (resp.stream().has_namespace_id()) {
    *ns_id = resp.stream().namespace_id();
  }

  for (auto id : resp.stream().table_id()) {
    object_ids->push_back(id);
  }

  options->clear();
  options->reserve(resp.stream().options_size());
  for (const auto& option : resp.stream().options()) {
    options->emplace(option.key(), option.value());
  }

  if (!resp.stream().has_namespace_id()) {
    options->emplace(cdc::kIdType, cdc::kTableId);
  }

  *transactional = cdc::StreamModeTransactional(resp.stream().transactional());

  if (replica_identity_map) {
    replica_identity_map->clear();
    replica_identity_map->reserve(resp.stream().replica_identity_map_size());
    for (const auto& entry : resp.stream().replica_identity_map()) {
        replica_identity_map->emplace(entry.first, entry.second);
    }
  }

  if (consistent_snapshot_time && resp.stream().has_cdcsdk_consistent_snapshot_time()) {
    *consistent_snapshot_time = resp.stream().cdcsdk_consistent_snapshot_time();
  }
  if (consistent_snapshot_option && resp.stream().has_cdcsdk_consistent_snapshot_option()) {
    *consistent_snapshot_option = resp.stream().cdcsdk_consistent_snapshot_option();
  }
  if (stream_creation_time && resp.stream().has_stream_creation_time()) {
    *stream_creation_time = resp.stream().stream_creation_time();
  }

  return Status::OK();
}

Result<CDCSDKStreamInfo> YBClient::GetCDCStream(
    const ReplicationSlotName& replication_slot_name,
    std::unordered_map<uint32_t, PgReplicaIdentity>* replica_identities) {
  GetCDCStreamRequestPB req;
  req.set_cdcsdk_ysql_replication_slot_name(replication_slot_name.ToString());

  GetCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetCDCStream);

  if (replica_identities) {
    replica_identities->reserve(resp.stream().replica_identity_map_size());
    for (const auto& entry : resp.stream().replica_identity_map()) {
      replica_identities->emplace(VERIFY_RESULT(GetPgsqlTableOid(entry.first)), entry.second);
    }
  }

  return CDCSDKStreamInfo::FromPB(resp.stream());
}

void YBClient::GetCDCStream(
    const xrepl::StreamId& stream_id,
    std::shared_ptr<TableId> table_id,
    std::shared_ptr<std::unordered_map<std::string, std::string>> options,
    StdStatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->GetCDCStream(this, stream_id, table_id, options, deadline, callback);
}

Result<std::vector<CDCSDKStreamInfo>> YBClient::ListCDCSDKStreams() {
  ListCDCStreamsRequestPB req;
  ListCDCStreamsResponsePB resp;

  req.set_id_type(master::IdTypePB::NAMESPACE_ID);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, ListCDCStreams);
  VLOG(4) << "The ListCDCStreamsResponsePB response: " << resp.DebugString();

  std::vector<CDCSDKStreamInfo> stream_infos;
  stream_infos.reserve(resp.streams_size());
  for (const auto& stream : resp.streams()) {
    // Skip CDCSDK streams which do not have a replication slot.
    if (!stream.has_cdcsdk_ysql_replication_slot_name()) {
        VLOG(4) << "Skipping stream " << stream.stream_id()
                << " since it does not have a cdcsdk_ysql_replication_slot_name";
        continue;
    }
    stream_infos.push_back(VERIFY_RESULT(CDCSDKStreamInfo::FromPB(stream)));
  }
  return stream_infos;
}

Status YBClient::DeleteCDCStream(
    const vector<xrepl::StreamId>& streams,
    bool force_delete,
    bool ignore_errors,
    master::DeleteCDCStreamResponsePB* ret) {
  if (streams.empty()) {
    return STATUS(InvalidArgument, "At least one stream id should be provided");
  }

  // Setting up request.
  DeleteCDCStreamRequestPB req;
  req.mutable_stream_id()->Reserve(narrow_cast<int>(streams.size()));
  for (const auto& stream : streams) {
    req.add_stream_id(stream.ToString());
  }
  req.set_force_delete(force_delete);
  req.set_ignore_errors(ignore_errors);

  if (ret) {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, (*ret), DeleteCDCStream);
  } else {
    DeleteCDCStreamResponsePB resp;
    CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, DeleteCDCStream);
  }

  return Status::OK();
}

Status YBClient::DeleteCDCStream(
    const xrepl::StreamId& stream_id, bool force_delete, bool ignore_errors) {
  // Setting up request.
  DeleteCDCStreamRequestPB req;
  req.add_stream_id(stream_id.ToString());
  req.set_force_delete(force_delete);
  req.set_ignore_errors(ignore_errors);

  DeleteCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, DeleteCDCStream);
  return Status::OK();
}

Status YBClient::DeleteCDCStream(
    const ReplicationSlotName& replication_slot_name, bool force_delete, bool ignore_errors) {
  // Setting up request.
  DeleteCDCStreamRequestPB req;
  req.add_cdcsdk_ysql_replication_slot_name(replication_slot_name.ToString());
  req.set_force_delete(force_delete);
  req.set_ignore_errors(ignore_errors);

  DeleteCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, DeleteCDCStream);
  return Status::OK();
}

void YBClient::DeleteCDCStream(const xrepl::StreamId& stream_id, StatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->DeleteCDCStream(this, stream_id, deadline, callback);
}

Status YBClient::GetCDCDBStreamInfo(
  const std::string &db_stream_id,
  std::vector<pair<std::string, std::string>>* db_stream_info) {
  // Setting up request.
  GetCDCDBStreamInfoRequestPB req;
  req.set_db_stream_id(db_stream_id);

  GetCDCDBStreamInfoResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetCDCDBStreamInfo);
  db_stream_info->clear();
  db_stream_info->reserve(resp.table_info_size());
  for (const auto& tabinfo : resp.table_info()) {
    std::string stream_id = tabinfo.stream_id();
    std::string table_id = tabinfo.table_id();

    db_stream_info->push_back(std::make_pair(stream_id, table_id));
  }

  return Status::OK();
}

void YBClient::GetCDCDBStreamInfo(
    const std::string& db_stream_id,
    const std::shared_ptr<std::vector<pair<std::string, std::string>>>& db_stream_info,
    const StdStatusCallback& callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->GetCDCDBStreamInfo(this, db_stream_id, db_stream_info, deadline, callback);
}

Status YBClient::UpdateCDCStream(
    const std::vector<xrepl::StreamId>& stream_ids,
    const std::vector<master::SysCDCStreamEntryPB>& new_entries) {
  if (stream_ids.size() != new_entries.size()) {
    return STATUS(InvalidArgument, "Mismatched number of stream IDs and entries.");
  }
  if (stream_ids.empty()) {
    return STATUS(InvalidArgument, "At least one stream ID is required.");
  }

  // Setting up request.
  UpdateCDCStreamRequestPB req;
  for (size_t i = 0; i < stream_ids.size(); i++) {
    SCHECK(stream_ids[i], InvalidArgument, "Stream ID cannot be empty.");

    auto stream = req.add_streams();
    stream->set_stream_id(stream_ids[i].ToString());
    stream->mutable_entry()->CopyFrom(new_entries[i]);
  }

  UpdateCDCStreamResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, UpdateCDCStream);
  return Status::OK();
}

Result<bool> YBClient::IsObjectPartOfXRepl(const TableId& table_id) {
  IsObjectPartOfXReplRequestPB req;
  IsObjectPartOfXReplResponsePB resp;
  req.set_table_id(table_id);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, IsObjectPartOfXRepl);
  return resp.has_error() ? StatusFromPB(resp.error().status())
                          : Result<bool>(resp.is_object_part_of_xrepl());
}

Result<bool> YBClient::IsBootstrapRequired(
    const std::vector<TableId>& table_ids, const boost::optional<xrepl::StreamId>& stream_id) {
  if (table_ids.empty()) {
    return STATUS(InvalidArgument, "At least one table ID is required.");
  }

  IsBootstrapRequiredRequestPB req;
  IsBootstrapRequiredResponsePB resp;
  for (const auto& table_id : table_ids) {
    req.add_table_ids(table_id);
  }
  if (stream_id) {
    req.add_stream_ids(stream_id->ToString());
  }
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, IsBootstrapRequired);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.results_size() != narrow_cast<int>(table_ids.size())) {
    return STATUS(IllegalState, Format("Expected $0 results, received: $1",
        table_ids.size(), resp.results_size()));
  }

  bool bootstrap_required = false;
  for (const auto& result : resp.results()) {
    if (result.bootstrap_required()) {
      bootstrap_required = true;
      break;
    }
  }

  return bootstrap_required;
}

Status YBClient::BootstrapProducer(
    const YQLDatabase& db_type,
    const NamespaceName& namespace_name,
    const std::vector<PgSchemaName>& pg_schema_names,
    const std::vector<TableName>& table_names,
    BootstrapProducerCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->BootstrapProducer(
      this, db_type, namespace_name, pg_schema_names, table_names, deadline, std::move(callback));
}

Status YBClient::UpdateConsumerOnProducerSplit(
    const xcluster::ReplicationGroupId& replication_group_id, const xrepl::StreamId& stream_id,
    const master::ProducerSplitTabletInfoPB& split_info) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Producer id is required.");
  SCHECK(stream_id, InvalidArgument, "Stream id is required.");

  UpdateConsumerOnProducerSplitRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_stream_id(stream_id.ToString());
  req.mutable_producer_split_tablet_info()->CopyFrom(split_info);

  UpdateConsumerOnProducerSplitResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, UpdateConsumerOnProducerSplit);
  return Status::OK();
}

Status YBClient::UpdateConsumerOnProducerMetadata(
    const xcluster::ReplicationGroupId& replication_group_id, const xrepl::StreamId& stream_id,
    const tablet::ChangeMetadataRequestPB& meta_info, uint32_t colocation_id,
    uint32_t producer_schema_version, uint32_t consumer_schema_version,
    master::UpdateConsumerOnProducerMetadataResponsePB* resp) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "ReplicationGroup id is required.");
  SCHECK(stream_id, InvalidArgument, "Stream id is required.");
  SCHECK(resp != nullptr, InvalidArgument, "Response pointer is required.");

  master::UpdateConsumerOnProducerMetadataRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_stream_id(stream_id.ToString());
  req.set_colocation_id(colocation_id);
  req.set_producer_schema_version(producer_schema_version);
  req.set_consumer_schema_version(consumer_schema_version);
  req.mutable_producer_change_metadata_request()->CopyFrom(meta_info);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, (*resp), UpdateConsumerOnProducerMetadata);
  return Status::OK();
}

Status YBClient::XClusterReportNewAutoFlagConfigVersion(
    const xcluster::ReplicationGroupId& replication_group_id, uint32 auto_flag_config_version) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "ReplicationGroup id is required");
  master::XClusterReportNewAutoFlagConfigVersionRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_auto_flag_config_version(auto_flag_config_version);

  master::XClusterReportNewAutoFlagConfigVersionResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, XClusterReportNewAutoFlagConfigVersion);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::AddTablesToUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id, const std::vector<TableId>& tables) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Producer id is required.");
  SCHECK(!tables.empty(), InvalidArgument, "Tables are required.");

  master::AlterUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  for (const auto& table : tables) {
    req.add_producer_table_ids_to_add(table);
  }

  master::AlterUniverseReplicationResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, AlterUniverseReplication);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::RemoveTablesFromUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id, const std::vector<TableId>& tables) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Producer id is required.");
  SCHECK(!tables.empty(), InvalidArgument, "Tables are required.");

  master::AlterUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  for (const auto& table : tables) {
    req.add_producer_table_ids_to_remove(table);
  }

  master::AlterUniverseReplicationResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, AlterUniverseReplication);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Result<HybridTime> YBClient::GetXClusterSafeTimeForNamespace(
    const NamespaceId& namespace_id, const master::XClusterSafeTimeFilter& filter) {
  SCHECK(!namespace_id.empty(), InvalidArgument, "Namespace id is required.");

  master::GetXClusterSafeTimeForNamespaceRequestPB req;
  req.set_namespace_id(namespace_id);
  req.set_filter(filter);

  master::GetXClusterSafeTimeForNamespaceResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetXClusterSafeTimeForNamespace);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return HybridTime::FromPB(resp.safe_time_ht());
}

void YBClient::DeleteNotServingTablet(const TabletId& tablet_id, StdStatusCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->DeleteNotServingTablet(this, tablet_id, deadline, callback);
}

void YBClient::GetTableLocations(
    const TableId& table_id, int32_t max_tablets, RequireTabletsRunning require_tablets_running,
    PartitionsOnly partitions_only, GetTableLocationsCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  data_->GetTableLocations(
      this, table_id, max_tablets, require_tablets_running, partitions_only, deadline,
      std::move(callback));
}

Status YBClient::TabletServerCount(int *tserver_count, bool primary_only,
                                   bool use_cache,
                                   const std::string* tablespace_id,
                                   const master::ReplicationInfoPB* replication_info) {
  // Must make an RPC call if replication info must be fetched
  // (ie. cannot use the tserver count cache)
  bool is_replication_info_required = tablespace_id || replication_info;
  SCHECK(!use_cache || !is_replication_info_required, InvalidArgument,
         "Cannot use cache when replication info must be fetched");

  int tserver_count_cached = data_->tserver_count_cached_[primary_only].load(
      std::memory_order_acquire);
  if (use_cache && tserver_count_cached > 0) {
    *tserver_count = tserver_count_cached;
    return Status::OK();
  }

  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  req.set_primary_only(primary_only);
  if (tablespace_id && !tablespace_id->empty())
    req.set_tablespace_id(*tablespace_id);
  if (replication_info)
    req.mutable_replication_info()->CopyFrom(*replication_info);
  // We should only refer to the TServers in the primary/sync cluster,
  // not the secondary read replica cluster.
  if (req.has_tablespace_id() || req.has_replication_info())
    req.set_primary_only(true);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, ListTabletServers);
  data_->tserver_count_cached_[primary_only].store(resp.servers_size(), std::memory_order_release);
  *tserver_count = resp.servers_size();
  return Status::OK();
}

Result<std::vector<YBTabletServer>> YBClient::ListTabletServers() {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  std::vector<YBTabletServer> result;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, ListTabletServers);
  result.reserve(resp.servers_size());
  for (int i = 0; i < resp.servers_size(); i++) {
    const ListTabletServersResponsePB_Entry& e = resp.servers(i);
    result.push_back(YBTabletServer::FromPB(e, data_->cloud_info_pb_));
  }
  return result;
}

Result<TabletServersInfo> YBClient::ListLiveTabletServers(bool primary_only) {
  ListLiveTabletServersRequestPB req;
  if (primary_only) {
    req.set_primary_only(true);
  }
  ListLiveTabletServersResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, ListLiveTabletServers);

  TabletServersInfo result;
  result.resize(resp.servers_size());
  for (int i = 0; i < resp.servers_size(); i++) {
    const ListLiveTabletServersResponsePB_Entry& entry = resp.servers(i);
    auto& out = result[i];
    out.server = YBTabletServer::FromPB(entry, data_->cloud_info_pb_);
    const CloudInfoPB& cloud_info = entry.registration().common().cloud_info();

    const auto& private_addresses = entry.registration().common().private_rpc_addresses();
    if (!private_addresses.empty()) {
      out.server.hostname = private_addresses.Get(0).host();
    }

    const auto& broadcast_addresses = entry.registration().common().broadcast_addresses();
    if (!broadcast_addresses.empty()) {
      out.public_ip = broadcast_addresses.Get(0).host();
    }

    out.is_primary = !entry.isfromreadreplica();
    if (cloud_info.has_placement_cloud()) {
      out.cloud = cloud_info.placement_cloud();
      if (cloud_info.has_placement_region()) {
        out.region = cloud_info.placement_region();
      }
      if (cloud_info.has_placement_zone()) {
        out.zone = cloud_info.placement_zone();
      }
    }
    out.pg_port = entry.registration().common().pg_port();
  }

  return result;
}

void YBClient::SetLocalTabletServer(const string& ts_uuid,
                                    const shared_ptr<tserver::TabletServerServiceProxy>& proxy,
                                    const tserver::LocalTabletServer* local_tserver) {
  data_->meta_cache_->SetLocalTabletServer(ts_uuid, proxy, local_tserver);
}

const internal::RemoteTabletServer* YBClient::GetLocalTabletServer() const {
  return data_->meta_cache_->local_tserver();
}

Result<bool> YBClient::IsLoadBalanced(uint32_t num_servers) {
  IsLoadBalancedRequestPB req;
  IsLoadBalancedResponsePB resp;

  req.set_expected_num_servers(num_servers);
  // Cannot use CALL_SYNC_LEADER_MASTER_RPC directly since this is substituted with RETURN_NOT_OK
  // and we want to capture the status to check if load is balanced.
  Status s = [&, this]() -> Status {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, IsLoadBalanced);
    return Status::OK();
  }();
  return s.ok();
}

Result<bool> YBClient::IsLoadBalancerIdle() {
  IsLoadBalancerIdleRequestPB req;
  IsLoadBalancerIdleResponsePB resp;

  Status s = [&]() -> Status {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, IsLoadBalancerIdle);
    return Status::OK();
  }();

  if (s.ok()) {
    return true;
  } else if (master::MasterError(s) == master::MasterErrorPB::LOAD_BALANCER_RECENTLY_ACTIVE) {
    return false;
  } else {
    return s;
  }
}

Status YBClient::ModifyTablePlacementInfo(const YBTableName& table_name,
                                          master::PlacementInfoPB&& live_replicas) {
  master::ReplicationInfoPB replication_info;
  // Merge the obtained info with the existing table replication info.
  std::shared_ptr<client::YBTable> table;
  RETURN_NOT_OK_PREPEND(OpenTable(table_name, &table), "Fetching table schema failed!");

  // If it does not exist, fetch the cluster replication info.
  if (!table->replication_info()) {
    GetMasterClusterConfigRequestPB req;
    GetMasterClusterConfigResponsePB resp;
    CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, GetMasterClusterConfig);
    master::SysClusterConfigEntryPB* sys_cluster_config_entry = resp.mutable_cluster_config();
    replication_info.CopyFrom(sys_cluster_config_entry->replication_info());
    // TODO(bogdan): Figure out how to handle read replias and leader affinity.
    replication_info.clear_read_replicas();
    replication_info.clear_affinitized_leaders();
    replication_info.clear_multi_affinitized_leaders();
  } else {
    // Table replication info exists, copy it over.
    replication_info.CopyFrom(table->replication_info().get());
  }

  // Put in the new live placement info.
  replication_info.mutable_live_replicas()->Swap(&live_replicas);

  std::unique_ptr<yb::client::YBTableAlterer> table_alterer(NewTableAlterer(table_name));
  return table_alterer->replication_info(replication_info)->Alter();
}

Status YBClient::CreateTransactionsStatusTable(
    const string& table_name, const master::ReplicationInfoPB* replication_info) {
  if (table_name.rfind(kTransactionTablePrefix, 0) != 0) {
    return STATUS_FORMAT(
        InvalidArgument, "Name '$0' for transaction table does not start with '$1'", table_name,
        kTransactionTablePrefix);
  }
  master::CreateTransactionStatusTableRequestPB req;
  master::CreateTransactionStatusTableResponsePB resp;
  req.set_table_name(table_name);
  if (replication_info) {
    *req.mutable_replication_info() = *replication_info;
  }
  CALL_SYNC_LEADER_MASTER_RPC_EX(Admin, req, resp, CreateTransactionStatusTable);
  return Status::OK();
}

Status YBClient::AddTransactionStatusTablet(const TableId& table_id) {
  master::AddTransactionStatusTabletRequestPB req;
  master::AddTransactionStatusTabletResponsePB resp;
  req.set_table_id(table_id);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Admin, req, resp, AddTransactionStatusTablet);
  return Status::OK();
}

Status YBClient::GetTabletsFromTableId(const string& table_id,
                                       const int32_t max_tablets,
                                       RepeatedPtrField<TabletLocationsPB>* tablets) {
  GetTableLocationsRequestPB req;
  GetTableLocationsResponsePB resp;
  req.mutable_table()->set_table_id(table_id);

  if (max_tablets == 0) {
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  } else if (max_tablets > 0) {
    req.set_max_returned_locations(max_tablets);
  }
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetTableLocations);
  *tablets = resp.tablet_locations();
  return Status::OK();
}

Status YBClient::GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            RepeatedPtrField<TabletLocationsPB>* tablets,
                            PartitionListVersion* partition_list_version,
                            const RequireTabletsRunning require_tablets_running,
                            const master::IncludeInactive include_inactive) {
  GetTableLocationsRequestPB req;
  GetTableLocationsResponsePB resp;
  if (table_name.has_table()) {
    table_name.SetIntoTableIdentifierPB(req.mutable_table());
  } else if (table_name.has_table_id()) {
    req.mutable_table()->set_table_id(table_name.table_id());
  }

  if (max_tablets == 0) {
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  } else if (max_tablets > 0) {
    req.set_max_returned_locations(max_tablets);
  }
  req.set_require_tablets_running(require_tablets_running);
  req.set_include_inactive(include_inactive);
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetTableLocations);
  *tablets = resp.tablet_locations();
  if (partition_list_version) {
    *partition_list_version = resp.partition_list_version();
  }
  return Status::OK();
}

Result<yb::master::GetTabletLocationsResponsePB> YBClient::GetTabletLocations(
    const std::vector<TabletId>& tablet_ids) {
  GetTabletLocationsRequestPB req;
  GetTabletLocationsResponsePB resp;
  req.mutable_tablet_ids()->Reserve(static_cast<int>(tablet_ids.size()));
  for (const auto& tablet_id : tablet_ids) {
    req.add_tablet_ids(tablet_id);
  }
  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetTabletLocations);

  return resp;
}

Result<TransactionStatusTablets> YBClient::GetTransactionStatusTablets(
    const CloudInfoPB& placement) {
  GetTransactionStatusTabletsRequestPB req;
  GetTransactionStatusTabletsResponsePB resp;

  req.mutable_placement()->CopyFrom(placement);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetTransactionStatusTablets);

  TransactionStatusTablets tablets;

  MoveCollection(&resp.global_tablet_id(), &tablets.global_tablets);
  MoveCollection(&resp.placement_local_tablet_id(), &tablets.placement_local_tablets);

  return tablets;
}

Result<int> YBClient::WaitForYsqlBackendsCatalogVersion(
    const std::string& database_name, uint64_t version, const MonoDelta& timeout) {
  // In order for timeout to approximately determine how much time is spent before responding,
  // incorporate the margin into the deadline because master will subtract the margin for
  // responding.
  CoarseTimePoint deadline = (
      CoarseMonoClock::Now()
      + MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms));
  if (!timeout.Initialized()) {
    deadline += MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms);
  } else {
    deadline += timeout;
  }
  return WaitForYsqlBackendsCatalogVersion(database_name, version, deadline);
}

Result<int> YBClient::WaitForYsqlBackendsCatalogVersion(
    const std::string& database_name, uint64_t version, const CoarseTimePoint& deadline) {
  GetNamespaceInfoResponsePB resp;
  RETURN_NOT_OK(GetNamespaceInfo("", database_name, YQL_DATABASE_PGSQL, &resp));
  PgOid database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(resp.namespace_().id()));
  return WaitForYsqlBackendsCatalogVersion(database_oid, version, deadline);
}

Result<int> YBClient::WaitForYsqlBackendsCatalogVersion(
    PgOid database_oid, uint64_t version, const MonoDelta& timeout) {
  // In order for timeout to approximately determine how much time is spent before responding,
  // incorporate the margin into the deadline because master will subtract the margin for
  // responding.
  CoarseTimePoint deadline = (
      CoarseMonoClock::Now()
      + MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms));
  if (!timeout.Initialized()) {
    deadline += MonoDelta::FromMilliseconds(
        FLAGS_wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms);
  } else {
    deadline += timeout;
  }
  return WaitForYsqlBackendsCatalogVersion(database_oid, version, deadline);
}

Result<int> YBClient::WaitForYsqlBackendsCatalogVersion(
    PgOid database_oid, uint64_t version, const CoarseTimePoint& deadline) {
  WaitForYsqlBackendsCatalogVersionRequestPB req;
  WaitForYsqlBackendsCatalogVersionResponsePB resp;

  req.set_database_oid(database_oid);
  req.set_catalog_version(version);

  DCHECK(deadline != CoarseTimePoint()) << ToString(deadline);

  CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(Admin, req, resp, deadline,
                                            WaitForYsqlBackendsCatalogVersion);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.num_lagging_backends();
}

Status YBClient::GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            vector<TabletId>* tablet_uuids,
                            vector<string>* ranges,
                            std::vector<master::TabletLocationsPB>* locations,
                            const RequireTabletsRunning require_tablets_running,
                            master::IncludeInactive include_inactive) {
  RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(GetTablets(
      table_name, max_tablets, &tablets, /* partition_list_version =*/ nullptr,
      require_tablets_running, include_inactive));
  FillFromRepeatedTabletLocations(tablets, tablet_uuids, ranges, locations);
  return Status::OK();
}

Status YBClient::GetTabletsAndUpdateCache(
    const YBTableName& table_name,
    const int32_t max_tablets,
    vector<TabletId>* tablet_uuids,
    vector<string>* ranges,
    std::vector<master::TabletLocationsPB>* locations) {
  RepeatedPtrField<TabletLocationsPB> tablets;
  PartitionListVersion partition_list_version;
  RETURN_NOT_OK(GetTablets(
      table_name, max_tablets, &tablets, &partition_list_version, RequireTabletsRunning::kFalse));
  FillFromRepeatedTabletLocations(tablets, tablet_uuids, ranges, locations);

  RETURN_NOT_OK(data_->meta_cache_->ProcessTabletLocations(
      tablets, partition_list_version, /* lookup_rpc = */ nullptr, AllowSplitTablet::kFalse));

  return Status::OK();
}

rpc::Messenger* YBClient::messenger() const {
  return data_->messenger_;
}

const scoped_refptr<MetricEntity>& YBClient::metric_entity() const {
  return data_->metric_entity_;
}

rpc::ProxyCache& YBClient::proxy_cache() const {
  return *data_->proxy_cache_;
}

ThreadPool *YBClient::callback_threadpool() {
  return data_->use_threadpool_for_callbacks_ ? data_->threadpool_.get() : nullptr;
}

const std::string& YBClient::proxy_uuid() const {
  return data_->uuid_;
}

const ClientId& YBClient::id() const {
  return data_->id_;
}

const CloudInfoPB& YBClient::cloud_info() const {
  return data_->cloud_info_pb_;
}

std::pair<RetryableRequestId, RetryableRequestId> YBClient::NextRequestIdAndMinRunningRequestId() {
  std::lock_guard lock(data_->tablet_requests_mutex_);
  auto& requests = data_->requests_;
  auto id = requests.request_id_seq++;
  requests.running_requests.insert(id);
  return std::make_pair(id, *requests.running_requests.begin());
}

void YBClient::AddMetaCacheInfo(JsonWriter* writer) {
  data_->meta_cache_->AddAllTabletInfo(writer);
}

void YBClient::RequestsFinished(const RetryableRequestIdRange& request_id_range) {
  if (request_id_range.empty()) {
    return;
  }
  std::lock_guard lock(data_->tablet_requests_mutex_);
  for (const auto& id : request_id_range) {
    auto& requests = data_->requests_.running_requests;
    auto it = requests.find(id);
    if (it != requests.end()) {
      requests.erase(it);
    } else {
      LOG_WITH_PREFIX(DFATAL) << "RequestsFinished called for an unknown request: "
                              << id;
    }
  }
}

void YBClient::LookupTabletByKey(const std::shared_ptr<YBTable>& table,
                                 const std::string& partition_key,
                                 CoarseTimePoint deadline,
                                 LookupTabletCallback callback) {
  data_->meta_cache_->LookupTabletByKey(table, partition_key, deadline, std::move(callback));
}

void YBClient::LookupTabletById(const std::string& tablet_id,
                                const std::shared_ptr<const YBTable>& table,
                                master::IncludeInactive include_inactive,
                                master::IncludeDeleted include_deleted,
                                CoarseTimePoint deadline,
                                LookupTabletCallback callback,
                                UseCache use_cache) {
  data_->meta_cache_->LookupTabletById(
      tablet_id, table, include_inactive, include_deleted, deadline, std::move(callback),
      use_cache);
}

void YBClient::LookupAllTablets(const std::shared_ptr<YBTable>& table,
                                CoarseTimePoint deadline,
                                LookupTabletRangeCallback callback) {
  data_->meta_cache_->LookupAllTablets(table, deadline, std::move(callback));
}

std::future<Result<internal::RemoteTabletPtr>> YBClient::LookupTabletByKeyFuture(
    const std::shared_ptr<YBTable>& table,
    const std::string& partition_key,
    CoarseTimePoint deadline) {
  return data_->meta_cache_->LookupTabletByKeyFuture(table, partition_key, deadline);
}

std::future<Result<std::vector<internal::RemoteTabletPtr>>> YBClient::LookupAllTabletsFuture(
    const std::shared_ptr<YBTable>& table,
    CoarseTimePoint deadline) {
  return MakeFuture<Result<std::vector<internal::RemoteTabletPtr>>>([&](auto callback) {
    this->LookupAllTablets(table, deadline, std::move(callback));
  });
}

Status YBClient::CreateSnapshot(
    const std::vector<YBTableName>& tables, CreateSnapshotCallback callback) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->CreateSnapshot(this, tables, deadline, std::move(callback));
}

Status YBClient::DeleteSnapshot(
    const TxnSnapshotId& snapshot_id, master::DeleteSnapshotResponsePB* ret) {
  master::DeleteSnapshotRequestPB req;
  if (!snapshot_id.IsNil()) req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());

  if (ret) {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Backup, req, (*ret), DeleteSnapshot);
  } else {
    master::DeleteSnapshotResponsePB resp;
    CALL_SYNC_LEADER_MASTER_RPC_EX(Backup, req, resp, DeleteSnapshot);
  }

  return Status::OK();
}

Result<RepeatedPtrField<master::SnapshotInfoPB>> YBClient::ListSnapshots(
    const TxnSnapshotId& snapshot_id, bool prepare_for_backup) {
  ListSnapshotsRequestPB req;
  ListSnapshotsResponsePB resp;
  req.set_prepare_for_backup(prepare_for_backup);
  if (!snapshot_id.IsNil()) req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());

  CALL_SYNC_LEADER_MASTER_RPC_EX(Backup, req, resp, ListSnapshots);

  return resp.snapshots();
}

HostPort YBClient::GetMasterLeaderAddress() {
  return data_->leader_master_hostport();
}

Status YBClient::ListMasters(CoarseTimePoint deadline, std::vector<std::string>* master_uuids) {
  ListMastersRequestPB req;
  ListMastersResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(Cluster, req, resp, deadline, ListMasters);

  master_uuids->clear();
  for (const ServerEntryPB& master : resp.masters()) {
    if (master.has_error()) {
      LOG_WITH_PREFIX(ERROR) << "Master " << master.ShortDebugString() << " hit error "
        << master.error().ShortDebugString();
      return StatusFromPB(master.error());
    }
    master_uuids->push_back(master.instance_id().permanent_uuid());
  }
  return Status::OK();
}

Result<HostPort> YBClient::RefreshMasterLeaderAddress() {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  RETURN_NOT_OK(data_->SetMasterServerProxy(deadline));

  return GetMasterLeaderAddress();
}

void YBClient::RefreshMasterLeaderAddressAsync() {
  data_->SetMasterServerProxyAsync(
      CoarseMonoClock::Now() + default_admin_operation_timeout(),
      false /* skip_resolution */, true /* wait_for_leader_election */, /* callback */ [](auto){});
}

Status YBClient::RemoveMasterFromClient(const HostPort& remove) {
  return data_->RemoveMasterAddress(remove);
}

Status YBClient::AddMasterToClient(const HostPort& add) {
  return data_->AddMasterAddress(add);
}

Status YBClient::SetMasterAddresses(const std::string& addrs) {
  return data_->SetMasterAddresses(addrs);
}

Status YBClient::GetMasterUUID(const string& host, uint16_t port, string* uuid) {
  HostPort hp(host, port);
  ServerEntryPB server;
  RETURN_NOT_OK(master::GetMasterEntryForHosts(
      data_->proxy_cache_.get(), {hp}, default_rpc_timeout(), &server));

  if (server.has_error()) {
    return STATUS_FORMAT(
      RuntimeError,
      "Error while getting uuid of $0.",
      HostPortToString(host, port));
  }

  *uuid = server.instance_id().permanent_uuid();

  return Status::OK();
}

Status YBClient::SetReplicationInfo(const ReplicationInfoPB& replication_info) {
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  return data_->SetReplicationInfo(this, replication_info, deadline);
}

Status YBClient::ValidateReplicationInfo(const ReplicationInfoPB& replication_info) {
  auto deadline = CoarseMonoClock::Now() + default_rpc_timeout();
  return data_->ValidateReplicationInfo(replication_info, deadline);
}

Result<TableSizeInfo> YBClient::GetTableDiskSize(const TableId& table_id) {
  auto deadline = CoarseMonoClock::Now() + default_rpc_timeout();
  return data_->GetTableDiskSize(table_id, deadline);
}

Status YBClient::ReportYsqlDdlTxnStatus(const TransactionMetadata& txn, bool is_committed) {
  auto deadline = CoarseMonoClock::Now() + default_rpc_timeout();
  return data_->ReportYsqlDdlTxnStatus(txn, is_committed, deadline);
}

Status YBClient::WaitForDdlVerificationToFinish(const TransactionMetadata& txn) {
  auto deadline = CoarseMonoClock::Now() +
      MonoDelta::FromSeconds(FLAGS_ddl_verification_timeout_multiplier *
                             default_admin_operation_timeout().ToSeconds());
  return data_->WaitForDdlVerificationToFinish(txn, deadline);
}

Result<bool> YBClient::CheckIfPitrActive() {
  auto deadline = CoarseMonoClock::Now() + default_rpc_timeout();
  return data_->CheckIfPitrActive(deadline);
}

Result<std::vector<YBTableName>> YBClient::ListTables(const std::string& filter,
                                                      bool exclude_ysql,
                                                      const std::string& ysql_db_filter,
                                                      bool skip_hidden) {
  ListTablesRequestPB req;
  ListTablesResponsePB resp;

  if (!filter.empty()) {
    req.set_name_filter(filter);
  }

  if (!ysql_db_filter.empty()) {
    req.mutable_namespace_()->set_name(ysql_db_filter);
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  }

  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTables);
  std::vector<YBTableName> result;
  result.reserve(resp.tables_size());

  for (int i = 0; i < resp.tables_size(); i++) {
    const ListTablesResponsePB_TableInfo& table_info = resp.tables(i);
    DCHECK(table_info.has_namespace_());
    DCHECK(table_info.namespace_().has_name());
    DCHECK(table_info.namespace_().has_id());
    if (exclude_ysql && table_info.table_type() == TableType::PGSQL_TABLE_TYPE) {
      continue;
    }
    if (skip_hidden && table_info.hidden()) {
      continue;
    }
    result.emplace_back(master::GetDatabaseTypeForTable(table_info.table_type()),
                        table_info.namespace_().id(),
                        table_info.namespace_().name(),
                        table_info.id(),
                        table_info.name(),
                        table_info.pgschema_name(),
                        table_info.relation_type());
  }
  return result;
}

Result<std::vector<YBTableName>> YBClient::ListUserTables(
    const master::NamespaceIdentifierPB& ns_identifier,
    bool include_indexes) {
  ListTablesRequestPB req;
  ListTablesResponsePB resp;

  req.mutable_namespace_()->CopyFrom(ns_identifier);
  req.add_relation_type_filter(master::USER_TABLE_RELATION);
  if (include_indexes) {
    req.add_relation_type_filter(master::INDEX_TABLE_RELATION);
  }
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTables);
  std::vector<YBTableName> result;
  result.reserve(resp.tables_size());

  for (int i = 0; i < resp.tables_size(); i++) {
    const ListTablesResponsePB_TableInfo& table_info = resp.tables(i);
    DCHECK(table_info.has_namespace_());
    DCHECK(table_info.namespace_().has_name());
    DCHECK(table_info.namespace_().has_id());
    result.emplace_back(master::GetDatabaseTypeForTable(table_info.table_type()),
                        table_info.namespace_().id(),
                        table_info.namespace_().name(),
                        table_info.id(),
                        table_info.name(),
                        table_info.pgschema_name(),
                        table_info.relation_type());
  }
  return result;
}

Status YBClient::AreNodesSafeToTakeDown(
      std::vector<std::string> tserver_uuids,
      std::vector<std::string> master_uuids,
      int follower_lag_bound_ms) {
  master::AreNodesSafeToTakeDownRequestPB req;
  master::AreNodesSafeToTakeDownResponsePB resp;

  for (auto& tserver_uuid : tserver_uuids) {
    req.add_tserver_uuids(std::move(tserver_uuid));
  }
  for (auto& master_uuid : master_uuids) {
    req.add_master_uuids(std::move(master_uuid));
  }
  req.set_follower_lag_bound_ms(follower_lag_bound_ms);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Admin, req, resp, AreNodesSafeToTakeDown);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Result<cdc::EnumOidLabelMap> YBClient::GetPgEnumOidLabelMap(const NamespaceName& ns_name) {
  GetUDTypeMetadataRequestPB req;
  GetUDTypeMetadataResponsePB resp;

  req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  req.mutable_namespace_()->set_name(ns_name);
  req.set_pg_enum_info(true);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetUDTypeMetadata);

  VLOG(1) << "For namespace " << ns_name << " found " << resp.enums_size() << " enums";

  cdc::EnumOidLabelMap enum_map;
  for (int i = 0; i < resp.enums_size(); i++) {
    const master::PgEnumInfoPB& enum_info = resp.enums(i);
    VLOG(1) << "Enum oid " << enum_info.oid() << " enum label: " << enum_info.label();
    enum_map.insert({enum_info.oid(), enum_info.label()});
  }
  return enum_map;
}

Result<cdc::CompositeAttsMap> YBClient::GetPgCompositeAttsMap(const NamespaceName& ns_name) {
  GetUDTypeMetadataRequestPB req;
  GetUDTypeMetadataResponsePB resp;

  req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  req.mutable_namespace_()->set_name(ns_name);
  req.set_pg_composite_info(true);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetUDTypeMetadata);

  VLOG(1) << "For namespace " << ns_name << " found " << resp.composites_size() << " composites";

  cdc::CompositeAttsMap type_atts_map;
  for (int i = 0; i < resp.composites_size(); i++) {
    const master::PgCompositeInfoPB& composite_info = resp.composites(i);
    VLOG(1) << "Composite type oid " << composite_info.oid()
            << " field count: " << composite_info.attributes_size();
    const vector<master::PgAttributePB>& atts{
        composite_info.attributes().begin(), composite_info.attributes().end()};
    type_atts_map.insert({composite_info.oid(), atts});
  }
  return type_atts_map;
}

Result<pair<Schema, uint32_t>> YBClient::GetTableSchemaFromSysCatalog(
    const TableId& table_id, const uint64_t read_time) {
  master::GetTableSchemaFromSysCatalogRequestPB req;
  master::GetTableSchemaFromSysCatalogResponsePB resp;

  req.mutable_table()->set_table_id(table_id);
  req.set_read_time(read_time);
  Schema current_schema;

  CALL_SYNC_LEADER_MASTER_RPC_EX(Replication, req, resp, GetTableSchemaFromSysCatalog);
  RETURN_NOT_OK(SchemaFromPB(resp.schema(), &current_schema));
  VLOG(1) << "For table_id " << table_id << " found specific schema version from system catalog.";

  return make_pair(current_schema, resp.version());
}

Result<bool> YBClient::TableExists(const YBTableName& table_name, bool skip_hidden) {
  auto tables = VERIFY_RESULT(ListTables(
      table_name.table_name(), /*exclude_ysql=*/false, /*ysql_db_filter=*/"", skip_hidden));
  for (const YBTableName& table : tables) {
    if (table == table_name) {
      return true;
    }
  }
  return false;
}

Status YBClient::OpenTable(const YBTableName& table_name, YBTablePtr* table) {
  return DoOpenTable(table_name, table);
}

Status YBClient::OpenTable(
    const TableId& table_id, YBTablePtr* table, master::GetTableSchemaResponsePB* resp) {
  return DoOpenTable(table_id, table, resp);
}

template <class Id>
Status YBClient::DoOpenTable(
    const Id& id, YBTablePtr* table, master::GetTableSchemaResponsePB* resp) {
  std::promise<Result<YBTablePtr>> result;
  DoOpenTableAsync(
      id, [&result](const auto& res) { result.set_value(res); }, resp);
  *table = VERIFY_RESULT(result.get_future().get());
  return Status::OK();
}

void YBClient::OpenTableAsync(
    const YBTableName& table_name, const OpenTableAsyncCallback& callback) {
  DoOpenTableAsync(table_name, callback);
}

void YBClient::OpenTableAsync(const TableId& table_id, const OpenTableAsyncCallback& callback,
                              master::GetTableSchemaResponsePB* resp) {
  DoOpenTableAsync(table_id, callback, resp);
}

template <class Id>
void YBClient::DoOpenTableAsync(const Id& id,
                                const OpenTableAsyncCallback& callback,
                                master::GetTableSchemaResponsePB* resp) {
  auto info = std::make_shared<YBTableInfo>();
  auto deadline = CoarseMonoClock::Now() + default_admin_operation_timeout();
  auto s = data_->GetTableSchema(
      this, id, deadline, info,
      Bind(&YBClient::GetTableSchemaCallback, Unretained(this), std::move(info), callback),
      resp);
  if (!s.ok()) {
    callback(s);
    return;
  }
}

void YBClient::GetTableSchemaCallback(std::shared_ptr<YBTableInfo> info,
                                      const OpenTableAsyncCallback& callback,
                                      const Status& s) {
  if (!s.ok()) {
    callback(s);
    return;
  }

  YBTable::FetchPartitions(
      this, info->table_id,
      [info, callback](const FetchPartitionsResult& fetch_result) {
        if (!fetch_result.ok()) {
          callback(fetch_result.status());
        } else {
          // In the future, probably will look up the table in some map to reuse YBTable instances.
          auto table = std::make_shared<YBTable>(*info, *fetch_result);
          callback(table);
        }
      });
}

shared_ptr<YBSession> YBClient::NewSession(MonoDelta delta) {
  return std::make_shared<YBSession>(this, delta);
}

shared_ptr<YBSession> YBClient::NewSession(CoarseTimePoint deadline) {
  return std::make_shared<YBSession>(this, deadline);
}

bool YBClient::IsMultiMaster() const {
  return data_->IsMultiMaster();
}

Result<int> YBClient::NumTabletsForUserTable(
    TableType table_type,
    const std::string* tablespace_id,
    const master::ReplicationInfoPB* replication_info) {
  if (table_type == TableType::PGSQL_TABLE_TYPE &&
        FLAGS_ysql_num_tablets > 0) {
    VLOG_WITH_PREFIX(1) << "num_tablets = " << FLAGS_ysql_num_tablets
                        << ": --ysql_num_tablets is specified.";
    return FLAGS_ysql_num_tablets;
  }

  if (table_type != TableType::PGSQL_TABLE_TYPE && FLAGS_ycql_num_tablets > 0) {
    VLOG_WITH_PREFIX(1) << "num_tablets = " << FLAGS_ycql_num_tablets
                        << ": --ycql_num_tablets is specified.";
    return FLAGS_ycql_num_tablets;
  }

  int tserver_count = 0;
  RETURN_NOT_OK(TabletServerCount(&tserver_count, true /* primary_only */,
        false /* use_cache */, tablespace_id, replication_info));
  SCHECK_GE(tserver_count, 0, IllegalState, "Number of tservers cannot be negative.");

  const auto num_tablets = GetInitialNumTabletsPerTable(table_type, tserver_count);
  VLOG_WITH_PREFIX(1) << "num_tablets = " << num_tablets
                      << " with " << tserver_count << " tservers.";

  return num_tablets;
}

void YBClient::TEST_set_admin_operation_timeout(const MonoDelta& timeout) {
  data_->default_admin_operation_timeout_ = timeout;
}

const MonoDelta& YBClient::default_admin_operation_timeout() const {
  return data_->default_admin_operation_timeout_;
}

const MonoDelta& YBClient::default_rpc_timeout() const {
  return data_->default_rpc_timeout_;
}

const uint64_t YBClient::kNoHybridTime = 0;

uint64_t YBClient::GetLatestObservedHybridTime() const {
  return data_->GetLatestObservedHybridTime();
}

void YBClient::SetLatestObservedHybridTime(uint64_t ht_hybrid_time) {
  data_->UpdateLatestObservedHybridTime(ht_hybrid_time);
}

CoarseTimePoint YBClient::PatchAdminDeadline(CoarseTimePoint deadline) const {
  if (deadline != CoarseTimePoint()) {
    return deadline;
  }
  return CoarseMonoClock::Now() + default_admin_operation_timeout();
}

Result<YBTablePtr> YBClient::OpenTable(const TableId& table_id) {
  YBTablePtr result;
  RETURN_NOT_OK(OpenTable(table_id, &result));
  return result;
}

Result<YBTablePtr> YBClient::OpenTable(const YBTableName& name) {
  YBTablePtr result;
  RETURN_NOT_OK(OpenTable(name, &result));
  return result;
}

Result<TableId> GetTableId(YBClient* client, const YBTableName& table_name) {
  return VERIFY_RESULT(client->GetYBTableInfo(table_name)).table_id;
}

const std::string& YBClient::LogPrefix() const {
  return data_->log_prefix_;
}

const std::string& YBClient::client_name() const {
  return data_->client_name_;
}

server::Clock* YBClient::Clock() const {
  return data_->clock_.get();
}

Result<encryption::UniverseKeyRegistryPB> YBClient::GetFullUniverseKeyRegistry() {
  master::GetFullUniverseKeyRegistryRequestPB req;
  master::GetFullUniverseKeyRegistryResponsePB resp;

  CALL_SYNC_LEADER_MASTER_RPC_EX(Encryption, req, resp, GetFullUniverseKeyRegistry);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.universe_key_registry();
}

Result<std::optional<AutoFlagsConfigPB>> YBClient::GetAutoFlagConfig() {
  master::GetAutoFlagsConfigRequestPB req;
  master::GetAutoFlagsConfigResponsePB resp;

  // CALL_SYNC_LEADER_MASTER_RPC_EX will return on failure. Capture the Status so that we can handle
  // the case when master is running on an older version that does not support this RPC.
  Status status = [&]() -> Status {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, GetAutoFlagsConfig);
    return Status::OK();
  }();

  if (status.ok()) {
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return std::move(resp.config());
  }

  if (rpc::RpcError(status) == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD) {
    return std::nullopt;
  }

  return status;
}

Result<std::optional<std::pair<bool, uint32>>> YBClient::ValidateAutoFlagsConfig(
    const AutoFlagsConfigPB& config, std::optional<AutoFlagClass> min_flag_class) {
  master::ValidateAutoFlagsConfigRequestPB req;
  master::ValidateAutoFlagsConfigResponsePB resp;
  req.mutable_config()->CopyFrom(config);
  if (min_flag_class) {
    req.set_min_flag_class(to_underlying(*min_flag_class));
  }

  // CALL_SYNC_LEADER_MASTER_RPC_EX will return on failure. Capture the Status so that we can handle
  // the case when master is running on an older version that does not support this RPC.
  Status status = [&]() -> Status {
    CALL_SYNC_LEADER_MASTER_RPC_EX(Cluster, req, resp, ValidateAutoFlagsConfig);
    return Status::OK();
  }();

  if (!status.ok()) {
    if (rpc::RpcError(status) == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD) {
      return std::nullopt;
    }

    return status;
  }
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  SCHECK(
      resp.has_valid() && resp.has_config_version(), IllegalState,
      "Invalid response from ValidateAutoFlagsConfig");
  return std::make_pair(resp.valid(), resp.config_version());
}

Result<master::StatefulServiceInfoPB> YBClient::GetStatefulServiceLocation(
    StatefulServiceKind service_kind) {
  master::GetStatefulServiceLocationRequestPB req;
  master::GetStatefulServiceLocationResponsePB resp;
  req.set_service_kind(service_kind);

  CALL_SYNC_LEADER_MASTER_RPC_EX(Client, req, resp, GetStatefulServiceLocation);

  RSTATUS_DCHECK(resp.has_service_info(), IllegalState, "No service info in response");
  RSTATUS_DCHECK(
      resp.service_info().has_permanent_uuid(), IllegalState, "No permanent uuid in response");

  return std::move(resp.service_info());
}

void YBClient::ClearAllMetaCachesOnServer() {
  data_->meta_cache_->ClearAll();
}

}  // namespace client
}  // namespace yb
