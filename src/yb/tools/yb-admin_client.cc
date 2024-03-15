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

#include "yb/tools/yb-admin_client.h"

#include <sstream>
#include <type_traits>
#include <unordered_map>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/tti/has_member_function.hpp>
#include <google/protobuf/util/json_util.h>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/xcluster_util.h"
#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_info.h"

#include "yb/common/colocated_util.h"
#include "yb/common/json_util.h"
#include "yb/common/ql_type_util.h"
#include "yb/common/redis_constants_common.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/strings/split.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_encryption.proxy.h"
#include "yb/master/master_error.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_test.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"
#include "yb/tools/yb-admin_util.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/encryption/encryption_util.h"

#include "yb/util/format.h"
#include "yb/util/net/net_util.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"
#include "yb/util/string_util.h"
#include "yb/util/flags.h"
#include "yb/util/tostring.h"

DEFINE_NON_RUNTIME_bool(wait_if_no_leader_master, false,
            "When yb-admin connects to the cluster and no leader master is present, "
            "this flag determines if yb-admin should wait for the entire duration of timeout or"
            "in case a leader master appears in that duration or return error immediately.");

DEFINE_NON_RUNTIME_string(certs_dir_name, "",
              "Directory with certificates to use for secure server connection.");

DEFINE_NON_RUNTIME_string(client_node_name, "", "Client node name.");

DEFINE_NON_RUNTIME_bool(disable_graceful_transition, false,
    "During a leader stepdown, disable graceful leadership transfer "
    "to an up to date peer");

DEFINE_test_flag(int32, metadata_file_format_version, 0,
    "Used in 'export_snapshot' metadata file format (0 means using latest format).");

DECLARE_bool(use_client_to_server_encryption);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

// Maximum number of elements to dump on unexpected errors.
static constexpr int MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR = 10;

PB_ENUM_FORMATTERS(yb::PeerRole);
PB_ENUM_FORMATTERS(yb::AppStatusPB::ErrorCode);
PB_ENUM_FORMATTERS(yb::tablet::RaftGroupStatePB);
PB_ENUM_FORMATTERS(yb::master::SysSnapshotEntryPB::State);

namespace yb {
namespace tools {

using namespace std::literals;

using std::cout;
using std::endl;
using std::string;
using std::pair;
using std::make_pair;
using std::vector;

using google::protobuf::RepeatedPtrField;
using google::protobuf::util::MessageToJsonString;

using client::YBClientBuilder;
using client::YBTableName;
using rpc::RpcController;
using pb_util::ParseFromSlice;
using strings::Substitute;
using tserver::TabletServerServiceProxy;
using tserver::TabletServerAdminServiceProxy;
using tserver::UpgradeYsqlRequestPB;
using tserver::UpgradeYsqlResponsePB;

using consensus::ConsensusServiceProxy;
using consensus::LeaderStepDownRequestPB;
using consensus::LeaderStepDownResponsePB;
using consensus::RaftPeerPB;
using consensus::RunLeaderElectionRequestPB;
using consensus::RunLeaderElectionResponsePB;

using master::BackupRowEntryPB;
using master::CreateSnapshotRequestPB;
using master::CreateSnapshotResponsePB;
using master::DeleteSnapshotRequestPB;
using master::DeleteSnapshotResponsePB;
using master::AbortSnapshotRestoreRequestPB;
using master::AbortSnapshotRestoreResponsePB;
using master::IdPairPB;
using master::ImportSnapshotMetaRequestPB;
using master::ImportSnapshotMetaResponsePB;
using master::ImportSnapshotMetaResponsePB_TableMetaPB;
using master::ListMastersRequestPB;
using master::ListMastersResponsePB;
using master::ListSnapshotRestorationsResponsePB;
using master::ListSnapshotsRequestPB;
using master::ListSnapshotsResponsePB;
using master::ListTablesRequestPB;
using master::ListTablesResponsePB;
using master::ListTabletServersRequestPB;
using master::ListTabletServersResponsePB;
using master::RestoreSnapshotRequestPB;
using master::RestoreSnapshotResponsePB;
using master::SnapshotInfoPB;
using master::SysNamespaceEntryPB;
using master::SysRowEntry;
using master::SysRowEntryType;
using master::SysSnapshotEntryPB;
using master::SysTablesEntryPB;
using master::SysTabletsEntryPB;
using master::SysUDTypeEntryPB;
using master::TabletLocationsPB;
using master::TSInfoPB;

namespace {

static constexpr const char* kRpcHostPortHeading = "RPC Host/Port";
static constexpr const char* kBroadcastHeading = "Broadcast Host/Port";
static constexpr const char* kTableIDPrefix = "tableid";

string FormatFirstHostPort(
    const RepeatedPtrField<HostPortPB>& rpc_addresses) {
  if (rpc_addresses.empty()) {
    return "N/A";
  } else {
    return HostPortPBToString(rpc_addresses.Get(0));
  }
}

string FormatDouble(double d, int precision = 2) {
  std::ostringstream op_stream;
  op_stream << std::fixed << std::setprecision(precision);
  op_stream << d;
  return op_stream.str();
}

const int kPartitionRangeColWidth = 56;
const int kHostPortColWidth = 20;
const int kTableNameColWidth = 48;
const int kNumCharactersInUuid = 32;
const int kLongColWidth = 15;
const int kSmallColWidth = 8;
const int kSleepTimeSec = 1;
const int kNumberOfTryouts = 30;

BOOST_TTI_HAS_MEMBER_FUNCTION(has_error)
template<typename T>
constexpr bool HasMemberFunctionHasError = has_member_function_has_error<const T, bool>::value;

template<class Response>
Result<Response> ResponseResult(Response&& response,
    typename std::enable_if<HasMemberFunctionHasError<Response>, void*>::type = nullptr) {
  // Response has has_error method, use status from it
  if(response.has_error()) {
    return StatusFromPB(response.error().status());
  }
  return std::move(response);
}

template<class Response>
Result<Response> ResponseResult(Response&& response,
    typename std::enable_if<!HasMemberFunctionHasError<Response>, void*>::type = nullptr) {
  // Response has no has_error method, nothing to check
  return std::move(response);
}

Result<TypedNamespaceName> ResolveNamespaceName(
    const Slice& prefix,
    const Slice& name,
    const YQLDatabase default_if_no_prefix = YQL_DATABASE_CQL) {
  auto db_type = YQL_DATABASE_UNKNOWN;
  if (!prefix.empty()) {
    static const std::array<pair<const char*, YQLDatabase>, 3> type_prefixes{
        make_pair(kDBTypePrefixCql, YQL_DATABASE_CQL),
        make_pair(kDBTypePrefixYsql, YQL_DATABASE_PGSQL),
        make_pair(kDBTypePrefixRedis, YQL_DATABASE_REDIS)};
    for (const auto& p : type_prefixes) {
      if (prefix == p.first) {
        db_type = p.second;
        break;
      }
    }

    if (db_type == YQL_DATABASE_UNKNOWN) {
      return STATUS_FORMAT(InvalidArgument, "Invalid db type name '$0'", prefix);
    }
  } else {
    db_type = (name == common::kRedisKeyspaceName ? YQL_DATABASE_REDIS : default_if_no_prefix);
  }
  return TypedNamespaceName{.db_type = db_type, .name = name.cdata()};
}

Slice GetTableIdAsSlice(const YBTableName& table_name) {
  return table_name.table_id();
}

Slice GetNamespaceIdAsSlice(const YBTableName& table_name) {
  return table_name.namespace_id();
}

Slice GetTableNameAsSlice(const YBTableName& table_name) {
  return table_name.table_name();
}

std::string FullNamespaceName(const master::NamespaceIdentifierPB& ns) {
  return Format("$0.$1", DatabasePrefix(ns.database_type()), ns.name());
}

struct NamespaceKey {
  explicit NamespaceKey(const master::NamespaceIdentifierPB& ns)
      : db_type(ns.database_type()), name(ns.name()) {
  }

  NamespaceKey(YQLDatabase d, const Slice& n)
      : db_type(d), name(n) {
  }

  YQLDatabase db_type;
  Slice name;
};

struct NamespaceComparator {
  using is_transparent = void;

  bool operator()(const master::NamespaceIdentifierPB& lhs,
                  const master::NamespaceIdentifierPB& rhs) const {
    return (*this)(NamespaceKey(lhs), NamespaceKey(rhs));
  }

  bool operator()(const master::NamespaceIdentifierPB& lhs, const NamespaceKey& rhs) const {
    return (*this)(NamespaceKey(lhs), rhs);
  }

  bool operator()(const NamespaceKey& lhs, const master::NamespaceIdentifierPB& rhs) const {
    return (*this)(lhs, NamespaceKey(rhs));
  }

  bool operator()(const NamespaceKey& lhs, const NamespaceKey& rhs) const {
    return lhs.db_type < rhs.db_type ||
           (lhs.db_type == rhs.db_type && lhs.name.compare(rhs.name) < 0);
  }
};

struct DotStringParts {
  Slice prefix;
  Slice value;
};

DotStringParts SplitByDot(const std::string& str) {
  const size_t dot_pos = str.find('.');
  DotStringParts result{.prefix = Slice(), .value = str};
  if (dot_pos != string::npos) {
    result.prefix = Slice(str.data(), dot_pos);
    result.value.remove_prefix(dot_pos + 1);
  }
  return result;
}

template <class Allocator>
Result<rapidjson::Value> SnapshotScheduleInfoToJson(
    const master::SnapshotScheduleInfoPB& schedule, Allocator* allocator) {
  rapidjson::Value json_schedule(rapidjson::kObjectType);
  AddStringField(
      "id", VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id())).ToString(), &json_schedule,
      allocator);

  const auto& filter = schedule.options().filter();
  string filter_output;
  // The user input should only have 1 entry, at namespace level.
  if (filter.tables().tables_size() == 1) {
    const auto& table_id = filter.tables().tables(0);
    if (table_id.has_namespace_()) {
      string database_type;
      if (table_id.namespace_().database_type() == YQL_DATABASE_PGSQL) {
        database_type = "ysql";
      } else if (table_id.namespace_().database_type() == YQL_DATABASE_CQL) {
        database_type = "ycql";
      }
      if (!database_type.empty()) {
        filter_output = Format("$0.$1", database_type, table_id.namespace_().name());
      }
    }
  }
  // If the user input was non standard, just display the whole debug PB.
  if (filter_output.empty()) {
    filter_output = filter.ShortDebugString();
    DCHECK(false) << "Non standard filter " << filter_output;
  }
  rapidjson::Value options(rapidjson::kObjectType);
  AddStringField("filter", filter_output, &options, allocator);
  auto interval_min = schedule.options().interval_sec() / MonoTime::kSecondsPerMinute;
  AddStringField("interval", Format("$0 min", interval_min), &options, allocator);
  auto retention_min = schedule.options().retention_duration_sec() / MonoTime::kSecondsPerMinute;
  AddStringField("retention", Format("$0 min", retention_min), &options, allocator);
  auto delete_time = HybridTime::FromPB(schedule.options().delete_time());
  if (delete_time) {
    AddStringField("delete_time", HybridTimeToString(delete_time), &options, allocator);
  }

  json_schedule.AddMember("options", options, *allocator);
  rapidjson::Value json_snapshots(rapidjson::kArrayType);
  for (const auto& snapshot : schedule.snapshots()) {
    rapidjson::Value json_snapshot(rapidjson::kObjectType);
    AddStringField(
        "id", VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id())).ToString(), &json_snapshot,
        allocator);
    auto snapshot_ht = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
    AddStringField("snapshot_time", HybridTimeToString(snapshot_ht), &json_snapshot, allocator);
    auto previous_snapshot_ht =
        HybridTime::FromPB(snapshot.entry().previous_snapshot_hybrid_time());
    if (previous_snapshot_ht) {
      AddStringField(
          "previous_snapshot_time", HybridTimeToString(previous_snapshot_ht), &json_snapshot,
          allocator);
    }
    json_snapshots.PushBack(json_snapshot, *allocator);
  }
  json_schedule.AddMember("snapshots", json_snapshots, *allocator);
  return json_schedule;
}

}  // anonymous namespace

class TableNameResolver::Impl {
 public:
  struct TableIdTag;
  struct TableNameTag;

  Impl(
      TableNameResolver::Values* values,
      std::vector<YBTableName>&& tables,
      vector<master::NamespaceIdentifierPB>&& namespaces)
      : current_namespace_(nullptr), values_(values) {
    std::move(tables.begin(), tables.end(), std::inserter(tables_, tables_.end()));
    std::move(namespaces.begin(), namespaces.end(), std::inserter(namespaces_, namespaces_.end()));
  }

  Result<bool> Feed(const std::string& str) {
    const auto result = FeedImpl(str);
    if (!result.ok()) {
      current_namespace_ = nullptr;
    }
    return result;
  }

  const master::NamespaceIdentifierPB* last_namespace() const { return current_namespace_; }

 private:
  Result<bool> FeedImpl(const std::string& str) {
    auto parts = SplitByDot(str);
    if (parts.prefix == kTableIDPrefix) {
      RETURN_NOT_OK(ProcessTableId(parts.value));
      return true;
    } else {
      if (!current_namespace_) {
        RETURN_NOT_OK(ProcessNamespace(parts.prefix, parts.value));
      } else {
        if (parts.prefix.empty()) {
          RETURN_NOT_OK(ProcessTableName(parts.value));
          return true;
        }
        return STATUS(InvalidArgument, "Wrong table name " + str);
      }
    }
    return false;
  }

  Status ProcessNamespace(const Slice& prefix, const Slice& value) {
    DCHECK(!current_namespace_);
    const auto ns = VERIFY_RESULT(ResolveNamespaceName(prefix, value));
    const auto i = namespaces_.find(NamespaceKey(ns.db_type, ns.name));
    if (i != namespaces_.end()) {
      current_namespace_ = &*i;
      return Status::OK();
    }
    return STATUS_FORMAT(
        InvalidArgument, "Namespace '$0' of type '$1' not found",
        ns.name, DatabasePrefix(ns.db_type));
  }

  Status ProcessTableId(const Slice& table_id) {
    const auto& idx = tables_.get<TableIdTag>();
    const auto i = idx.find(table_id);
    if (i == idx.end()) {
      return STATUS_FORMAT(InvalidArgument, "Table with id '$0' not found", table_id);
    }
    if (current_namespace_ && current_namespace_->id() != i->namespace_id()) {
      return STATUS_FORMAT(
          InvalidArgument, "Table with id '$0' belongs to different namespace '$1'",
          table_id, FullNamespaceName(*current_namespace_));
    }
    AppendTable(*i);
    return Status::OK();
  }

  Status ProcessTableName(const Slice& table_name) {
    DCHECK(current_namespace_);
    const auto& idx = tables_.get<TableNameTag>();
    const auto key = boost::make_tuple(Slice(current_namespace_->id()), table_name);
    // For some reason idx.equal_range(key) failed to compile.
    const auto range = std::make_pair(idx.lower_bound(key), idx.upper_bound(key));
    switch (std::distance(range.first, range.second)) {
      case 0:
        return STATUS_FORMAT(
            InvalidArgument, "Table with name '$0' not found in namespace '$1'",
            table_name, FullNamespaceName(*current_namespace_));
      case 1:
        AppendTable(*range.first);
        return Status::OK();
      default:
        return STATUS_FORMAT(
            InvalidArgument,
            "Namespace '$0' has multiple tables named '$1', specify table id instead",
            FullNamespaceName(*current_namespace_), table_name);
    }
  }

  void AppendTable(const YBTableName& table) {
    current_namespace_ = nullptr;
    values_->push_back(table);
  }

  using TableContainer = boost::multi_index_container<YBTableName,
      boost::multi_index::indexed_by<
          boost::multi_index::ordered_unique<
              boost::multi_index::tag<TableIdTag>,
              boost::multi_index::global_fun<const YBTableName&, Slice, &GetTableIdAsSlice>,
              Slice::Comparator
          >,
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<TableNameTag>,
              boost::multi_index::composite_key<
                  YBTableName,
                  boost::multi_index::global_fun<const YBTableName&, Slice, &GetNamespaceIdAsSlice>,
                  boost::multi_index::global_fun<const YBTableName&, Slice, &GetTableNameAsSlice>
              >,
              boost::multi_index::composite_key_compare<
                  Slice::Comparator,
                  Slice::Comparator
              >
          >
      >
  >;

  TableContainer tables_;
  std::set<master::NamespaceIdentifierPB, NamespaceComparator> namespaces_;
  const master::NamespaceIdentifierPB* current_namespace_;
  TableNameResolver::Values* values_;
};

TableNameResolver::TableNameResolver(
    std::vector<client::YBTableName>* container,
    std::vector<client::YBTableName>&& tables,
    std::vector<master::NamespaceIdentifierPB>&& namespaces)
    : impl_(new Impl(container, std::move(tables), std::move(namespaces))) {}

TableNameResolver::TableNameResolver(TableNameResolver&&) = default;

TableNameResolver::~TableNameResolver() = default;

Result<bool> TableNameResolver::Feed(const std::string& value) {
  return impl_->Feed(value);
}

const master::NamespaceIdentifierPB* TableNameResolver::last_namespace() const {
  return impl_->last_namespace();
}

ClusterAdminClient::ClusterAdminClient(string addrs, MonoDelta timeout)
    : master_addr_list_(std::move(addrs)),
      timeout_(timeout),
      initted_(false) {}

ClusterAdminClient::ClusterAdminClient(const HostPort& init_master_addr, MonoDelta timeout)
    : init_master_addr_(init_master_addr),
      timeout_(timeout),
      initted_(false) {}

ClusterAdminClient::~ClusterAdminClient() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status ClusterAdminClient::DiscoverAllMasters(
    const HostPort& init_master_addr,
    std::string* all_master_addrs) {

  master::MasterClusterProxy proxy(proxy_cache_.get(), init_master_addr);

  VLOG(0) << "Initializing master leader list from single master at "
          << init_master_addr.ToString();
  const auto list_resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::ListMasters, proxy, ListMastersRequestPB()));
  if (list_resp.masters().empty()) {
    return  STATUS(NotFound, "no masters found");
  }

  std::vector<std::string> addrs;
  for (const auto& master : list_resp.masters()) {
    if (!master.has_registration()) {
      LOG(WARNING) << master.instance_id().permanent_uuid() << " has no registration.";
      continue;
    }

    if (master.registration().broadcast_addresses_size() > 0) {
      addrs.push_back(FormatFirstHostPort(master.registration().broadcast_addresses()));
    } else if (master.registration().private_rpc_addresses_size() > 0) {
      addrs.push_back(FormatFirstHostPort(master.registration().private_rpc_addresses()));
    } else {
      LOG(WARNING) << master.instance_id().permanent_uuid() << " has no rpc/broadcast address.";
      continue;
    }
  }

  if (addrs.empty()) {
    return STATUS(NotFound, "no masters found");
  }

  JoinStrings(addrs, ",", all_master_addrs);
  VLOG(0) << "Discovered full master list: " << *all_master_addrs;
  return Status::OK();
}

Status ClusterAdminClient::Init() {
  CHECK(!initted_);

  // Check if caller will initialize the client and related parts.
  rpc::MessengerBuilder messenger_builder("yb-admin");
  if (!FLAGS_certs_dir_name.empty()) {
    LOG(INFO) << "Built secure client using certs dir " << FLAGS_certs_dir_name;
    const auto& cert_name = FLAGS_client_node_name;
    secure_context_ = VERIFY_RESULT(server::CreateSecureContext(
        FLAGS_certs_dir_name, server::UseClientCerts(!cert_name.empty()), cert_name));
    server::ApplySecureContext(secure_context_.get(), &messenger_builder);
  }

  messenger_ = VERIFY_RESULT(messenger_builder.Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

  if (!init_master_addr_.host().empty()) {
    RETURN_NOT_OK(DiscoverAllMasters(init_master_addr_, &master_addr_list_));
  }

  yb_client_ = VERIFY_RESULT(YBClientBuilder()
      .add_master_server_addr(master_addr_list_)
      .default_admin_operation_timeout(timeout_)
      .wait_for_leader_election_on_init(FLAGS_wait_if_no_leader_master)
      .Build(messenger_.get()));

  ResetMasterProxy();

  initted_ = true;
  return Status::OK();
}

void ClusterAdminClient::ResetMasterProxy(const HostPort& leader_addr) {
  // Find the leader master's socket info to set up the master proxy.
  if (leader_addr.host().empty()) {
    leader_addr_ = yb_client_->GetMasterLeaderAddress();
  } else {
    leader_addr_ = leader_addr;
  }

  master_admin_proxy_ = std::make_unique<master::MasterAdminProxy>(
      proxy_cache_.get(), leader_addr_);

  master_backup_proxy_ = std::make_unique<master::MasterBackupProxy>(
      proxy_cache_.get(), leader_addr_);

  master_client_proxy_ = std::make_unique<master::MasterClientProxy>(
      proxy_cache_.get(), leader_addr_);

  master_cluster_proxy_ = std::make_unique<master::MasterClusterProxy>(
      proxy_cache_.get(), leader_addr_);

  master_ddl_proxy_ = std::make_unique<master::MasterDdlProxy>(
      proxy_cache_.get(), leader_addr_);

  master_encryption_proxy_ = std::make_unique<master::MasterEncryptionProxy>(
      proxy_cache_.get(), leader_addr_);

  master_replication_proxy_ = std::make_unique<master::MasterReplicationProxy>(
      proxy_cache_.get(), leader_addr_);

  master_test_proxy_ = std::make_unique<master::MasterTestProxy>(
      proxy_cache_.get(), leader_addr_);
}

Status ClusterAdminClient::MasterLeaderStepDown(
    const string& leader_uuid,
    const string& new_leader_uuid) {
  auto master_proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_.get(), leader_addr_);

  return LeaderStepDown(leader_uuid, yb::master::kSysCatalogTabletId,
      new_leader_uuid, &master_proxy);
}

Status ClusterAdminClient::LeaderStepDownWithNewLeader(
    const std::string& tablet_id,
    const std::string& dest_ts_uuid) {
  return LeaderStepDown(
      /* leader_uuid */ std::string(),
      tablet_id,
      dest_ts_uuid,
      /* leader_proxy */ nullptr);
}

Status ClusterAdminClient::LeaderStepDown(
    const PeerId& leader_uuid,
    const TabletId& tablet_id,
    const PeerId& new_leader_uuid,
    std::unique_ptr<ConsensusServiceProxy>* leader_proxy) {
  LeaderStepDownRequestPB req;
  req.set_tablet_id(tablet_id);
  if (!new_leader_uuid.empty()) {
    req.set_new_leader_uuid(new_leader_uuid);
  } else {
    if (FLAGS_disable_graceful_transition) {
      req.set_disable_graceful_transition(true);
    }
  }
  // The API for InvokeRpcNoResponseCheck requires a raw pointer to a ConsensusServiceProxy, so
  // cache it outside, if we are creating a new proxy to a previously unknown leader.
  std::unique_ptr<ConsensusServiceProxy> new_proxy;
  if (!leader_uuid.empty()) {
    // TODO: validate leader_proxy ?
    req.set_dest_uuid(leader_uuid);
  } else {
    // Look up the location of the tablet leader from the Master.
    HostPort leader_addr;
    string lookup_leader_uuid;
    RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &lookup_leader_uuid, &leader_addr));
    req.set_dest_uuid(lookup_leader_uuid);
    new_proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_.get(), leader_addr);
  }
  VLOG(2) << "Sending request " << req.DebugString() << " to node with uuid [" << leader_uuid
          << "]";
  const auto resp = VERIFY_RESULT(InvokeRpcNoResponseCheck(&ConsensusServiceProxy::LeaderStepDown,
      *(new_proxy ? new_proxy.get() : leader_proxy->get()),
      req));
  if (resp.has_error()) {
    LOG(ERROR) << "LeaderStepDown for " << leader_uuid << "received error "
      << resp.error().ShortDebugString();
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

// Force start an election on a randomly chosen non-leader peer of this tablet's raft quorum.
Status ClusterAdminClient::StartElection(const TabletId& tablet_id) {
  HostPort non_leader_addr;
  string non_leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, FOLLOWER, &non_leader_uuid, &non_leader_addr));
  ConsensusServiceProxy non_leader_proxy(proxy_cache_.get(), non_leader_addr);
  RunLeaderElectionRequestPB req;
  req.set_dest_uuid(non_leader_uuid);
  req.set_tablet_id(tablet_id);
  return ResultToStatus(InvokeRpc(
      &ConsensusServiceProxy::RunLeaderElection, non_leader_proxy, req));
}

// Look up the location of the tablet server leader or non-leader peer from the leader master
Status ClusterAdminClient::SetTabletPeerInfo(
    const TabletId& tablet_id,
    PeerMode mode,
    PeerId* peer_uuid,
    HostPort* peer_addr) {
  TSInfoPB peer_ts_info;
  RETURN_NOT_OK(GetTabletPeer(tablet_id, mode, &peer_ts_info));
  auto rpc_addresses = peer_ts_info.private_rpc_addresses();
  CHECK_GT(rpc_addresses.size(), 0) << peer_ts_info
        .ShortDebugString();

  *peer_addr = HostPortFromPB(rpc_addresses.Get(0));
  *peer_uuid = peer_ts_info.permanent_uuid();
  return Status::OK();
}

Status ClusterAdminClient::SetWalRetentionSecs(
  const YBTableName& table_name,
  const uint32_t wal_ret_secs) {
  auto alterer = yb_client_->NewTableAlterer(table_name);
  RETURN_NOT_OK(alterer->SetWalRetentionSecs(wal_ret_secs)->Alter());
  cout << "Set table " << table_name.table_name() << " WAL retention time to " << wal_ret_secs
       << " seconds." << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetWalRetentionSecs(const YBTableName& table_name) {
  const auto info = VERIFY_RESULT(yb_client_->GetYBTableInfo(table_name));
  if (!info.wal_retention_secs) {
    cout << "WAL retention time not set for table " << table_name.table_name() << endl;
  } else {
    cout << "Found WAL retention time for table " << table_name.table_name() << ": "
         << info.wal_retention_secs.get() << " seconds" << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::GetAutoFlagsConfig() {
  master::GetAutoFlagsConfigRequestPB req;
  master::GetAutoFlagsConfigResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_cluster_proxy_->GetAutoFlagsConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "AutoFlags config:" << std::endl;
  std::cout << resp.config().DebugString() << std::endl;

  return Status::OK();
}

Status ClusterAdminClient::PromoteAutoFlags(
    const string& max_flag_class, const bool promote_non_runtime_flags, const bool force) {
  master::PromoteAutoFlagsRequestPB req;
  master::PromoteAutoFlagsResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  req.set_max_flag_class(max_flag_class);
  req.set_promote_non_runtime_flags(promote_non_runtime_flags);
  req.set_force(force);
  RETURN_NOT_OK(master_cluster_proxy_->PromoteAutoFlags(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "PromoteAutoFlags completed successfully" << std::endl;
  if (!resp.flags_promoted()) {
    std::cout << "No new AutoFlags eligible to promote" << std::endl;
    if (resp.has_new_config_version()) {
      std::cout << "Current config version: " << resp.new_config_version() << std::endl;
    }
    return Status::OK();
  }
    std::cout << "New AutoFlags were promoted" << std::endl;
    std::cout << "New config version: " << resp.new_config_version() << std::endl;
    if (resp.non_runtime_flags_promoted()) {
      std::cout << "All yb-master and yb-tserver processes need to be restarted in order to apply "
                   "the promoted AutoFlags"
                << std::endl;
    }

  return Status::OK();
}

Status ClusterAdminClient::RollbackAutoFlags(uint32_t rollback_version) {
  master::RollbackAutoFlagsRequestPB req;
  master::RollbackAutoFlagsResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);
  req.set_rollback_version(rollback_version);
  RETURN_NOT_OK(master_cluster_proxy_->RollbackAutoFlags(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "RollbackAutoFlags completed successfully" << std::endl;
  if (!resp.flags_rolledback()) {
    std::cout << "No AutoFlags have been promoted since version " << rollback_version << std::endl;
    std::cout << "Current config version: " << resp.new_config_version() << std::endl;
    return Status::OK();
  }

    std::cout << "AutoFlags that were promoted after config version " << rollback_version
              << " were successfully rolled back" << std::endl;
    std::cout << "New config version: " << resp.new_config_version() << std::endl;

    return Status::OK();
}

Status ClusterAdminClient::PromoteSingleAutoFlag(
    const std::string& process_name, const std::string& flag_name) {
    master::PromoteSingleAutoFlagRequestPB req;
    master::PromoteSingleAutoFlagResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    req.set_process_name(process_name);
    req.set_auto_flag_name(flag_name);
    RETURN_NOT_OK(master_cluster_proxy_->PromoteSingleAutoFlag(req, &resp, &rpc));
    if (resp.has_error()) {
     return StatusFromPB(resp.error().status());
    }
    if (!resp.flag_promoted()) {
      std::cout << "Failed to promote AutoFlag " << flag_name << " from process " << process_name
                << ". Check the logs for more information" << std::endl;
      std::cout << "Current config version: " << resp.new_config_version() << std::endl;
      return Status::OK();
    }

    std::cout << "AutoFlag " << flag_name << " from process " << process_name
              << " was successfully promoted" << std::endl;
    std::cout << "New config version: " << resp.new_config_version() << std::endl;
    if (resp.non_runtime_flag_promoted()) {
     std::cout << "All yb-master and yb-tserver processes need to be restarted in order to apply "
                  "the promoted AutoFlag"
               << std::endl;
    }

    return Status::OK();
}

Status ClusterAdminClient::DemoteSingleAutoFlag(
    const std::string& process_name, const std::string& flag_name) {
    master::DemoteSingleAutoFlagRequestPB req;
    master::DemoteSingleAutoFlagResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(timeout_);
    req.set_process_name(process_name);
    req.set_auto_flag_name(flag_name);
    RETURN_NOT_OK(master_cluster_proxy_->DemoteSingleAutoFlag(req, &resp, &rpc));
    if (resp.has_error()) {
     return StatusFromPB(resp.error().status());
    }
    if (!resp.flag_demoted()) {
      std::cout << "Unable to demote AutoFlag " << flag_name << " from process " << process_name
                << " because the flag is not in promoted state" << std::endl;
      std::cout << "Current config version: " << resp.new_config_version() << std::endl;
      return Status::OK();
    }

    std::cout << "AutoFlag " << flag_name << " from process " << process_name
              << " was successfully demoted" << std::endl;
    std::cout << "New config version: " << resp.new_config_version() << std::endl;

    return Status::OK();
}

Status ClusterAdminClient::ParseChangeType(
    const string& change_type,
    consensus::ChangeConfigType* cc_type) {
  consensus::ChangeConfigType cctype = consensus::UNKNOWN_CHANGE;
  *cc_type = cctype;
  string uppercase_change_type;
  ToUpperCase(change_type, &uppercase_change_type);
  if (!consensus::ChangeConfigType_Parse(uppercase_change_type, &cctype) ||
    cctype == consensus::UNKNOWN_CHANGE) {
    return STATUS(InvalidArgument, "Unsupported change_type", change_type);
  }

  *cc_type = cctype;

  return Status::OK();
}

Status ClusterAdminClient::ChangeConfig(
    const TabletId& tablet_id,
    const string& change_type,
    const PeerId& peer_uuid,
    const boost::optional<string>& member_type) {
  CHECK(initted_);

  consensus::ChangeConfigType cc_type;
  RETURN_NOT_OK(ParseChangeType(change_type, &cc_type));

  RaftPeerPB peer_pb;
  peer_pb.set_permanent_uuid(peer_uuid);

  // Parse the optional fields.
  if (member_type) {
    consensus::PeerMemberType member_type_val;
    string uppercase_member_type;
    ToUpperCase(*member_type, &uppercase_member_type);
    if (!PeerMemberType_Parse(uppercase_member_type, &member_type_val)) {
      return STATUS(InvalidArgument, "Unrecognized member_type", *member_type);
    }
    if (member_type_val != consensus::PeerMemberType::PRE_VOTER &&
        member_type_val != consensus::PeerMemberType::PRE_OBSERVER) {
      return STATUS(InvalidArgument, "member_type should be PRE_VOTER or PRE_OBSERVER");
    }
    peer_pb.set_member_type(member_type_val);
  }

  // Validate the existence of the optional fields.
  if (!member_type && cc_type == consensus::ADD_SERVER) {
    return STATUS(InvalidArgument, "Must specify member_type when adding a server.");
  }

  // Look up RPC address of peer if adding as a new server.
  if (cc_type == consensus::ADD_SERVER) {
    HostPort host_port = VERIFY_RESULT(GetFirstRpcAddressForTS(peer_uuid));
    HostPortToPB(host_port, peer_pb.mutable_last_known_private_addr()->Add());
  }

  // Look up the location of the tablet leader from the Master.
  HostPort leader_addr;
  string leader_uuid;
  RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &leader_uuid, &leader_addr));

  auto consensus_proxy = std::make_unique<ConsensusServiceProxy>(proxy_cache_.get(), leader_addr);
  // If removing the leader ts, then first make it step down and that
  // starts an election and gets a new leader ts.
  if (cc_type == consensus::REMOVE_SERVER &&
      leader_uuid == peer_uuid) {
    string old_leader_uuid = leader_uuid;
    RETURN_NOT_OK(LeaderStepDown(
          leader_uuid, tablet_id, /* new_leader_uuid */ std::string(), &consensus_proxy));
    sleep(5);  // TODO - election completion timing is not known accurately
    RETURN_NOT_OK(SetTabletPeerInfo(tablet_id, LEADER, &leader_uuid, &leader_addr));
    if (leader_uuid == old_leader_uuid) {
      return STATUS(ConfigurationError,
                    "Old tablet server leader same as new even after re-election!");
    }
    consensus_proxy.reset(new ConsensusServiceProxy(proxy_cache_.get(), leader_addr));
  }

  consensus::ChangeConfigRequestPB req;
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(tablet_id);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;
  return ResultToStatus(InvokeRpc(
      &ConsensusServiceProxy::ChangeConfig, *consensus_proxy, req));
}

Result<std::string> ClusterAdminClient::GetMasterLeaderUuid() {
  std::string leader_uuid;
  const auto list_resp = VERIFY_RESULT_PREPEND(
      InvokeRpc(
          &master::MasterClusterProxy::ListMasters, *master_cluster_proxy_,
          ListMastersRequestPB()),
      "Could not locate master leader");
  for (const auto& master : list_resp.masters()) {
    if (master.role() == PeerRole::LEADER) {
      SCHECK(
          leader_uuid.empty(), ConfigurationError, "Found two LEADER's in the same raft config.");
      leader_uuid = master.instance_id().permanent_uuid();
    }
  }
  SCHECK(!leader_uuid.empty(), ConfigurationError, "Could not locate master leader!");
  return std::move(leader_uuid);
}

Status ClusterAdminClient::DumpMasterState(bool to_console) {
  CHECK(initted_);
  master::DumpMasterStateRequestPB req;
  req.set_peers_also(true);
  req.set_on_disk(true);
  req.set_return_dump_as_string(to_console);

  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::DumpState, *master_cluster_proxy_, req));

  if (to_console) {
    cout << resp.dump() << endl;
  } else {
    cout << "Master state dump has been completed and saved into "
            "the master respective log files." << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::GetLoadMoveCompletion() {
  CHECK(initted_);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::GetLoadMoveCompletion, *master_cluster_proxy_,
      master::GetLoadMovePercentRequestPB()));
  cout << "Percent complete = " << resp.percent() << " : "
    << resp.remaining() << " remaining out of " << resp.total() << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetLeaderBlacklistCompletion() {
  CHECK(initted_);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::GetLeaderBlacklistCompletion, *master_cluster_proxy_,
      master::GetLeaderBlacklistPercentRequestPB()));
  cout << "Percent complete = " << resp.percent() << " : "
    << resp.remaining() << " remaining out of " << resp.total() << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetIsLoadBalancerIdle() {
  CHECK(initted_);

  const bool is_idle = VERIFY_RESULT(yb_client_->IsLoadBalancerIdle());
  cout << "Idle = " << is_idle << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListLeaderCounts(const YBTableName& table_name) {
  std::unordered_map<string, int> leader_counts = VERIFY_RESULT(GetLeaderCounts(table_name));
  int total_leader_count = 0;
  for (const auto& lc : leader_counts) { total_leader_count += lc.second; }

  // Calculate the standard deviation and adjusted deviation percentage according to the best and
  // worst-case scenarios. Best-case distribution is when leaders are evenly distributed and
  // worst-case is when leaders are all on one tablet server.
  // For example, say we have 16 leaders on 3 tablet servers:
  //   Leader distribution:    7 5 4
  //   Best-case scenario:     6 5 5
  //   Worst-case scenario:   12 0 0
  //   Standard deviation:    1.24722
  //   Adjusted deviation %:  10.9717%
  vector<double> leader_dist, best_case, worst_case;
  cout << RightPadToUuidWidth("Server UUID") << kColumnSep << "Leader Count" << endl;
  for (const auto& leader_count : leader_counts) {
    cout << leader_count.first << kColumnSep << leader_count.second << endl;
    leader_dist.push_back(leader_count.second);
  }

  if (!leader_dist.empty()) {
    for (size_t i = 0; i < leader_dist.size(); ++i) {
      best_case.push_back(total_leader_count / leader_dist.size());
      worst_case.push_back(0);
    }
    for (size_t i = 0; i < total_leader_count % leader_dist.size(); ++i) {
      ++best_case[i];
    }
    worst_case[0] = total_leader_count;

    double stdev = yb::standard_deviation(leader_dist);
    double best_stdev = yb::standard_deviation(best_case);
    double worst_stdev = yb::standard_deviation(worst_case);
    double percent_dev = (stdev - best_stdev) / (worst_stdev - best_stdev) * 100.0;
    cout << "Standard deviation: " << stdev << endl;
    cout << "Adjusted deviation percentage: " << percent_dev << "%" << endl;
  }

  return Status::OK();
}

Result<std::unordered_map<string, int>> ClusterAdminClient::GetLeaderCounts(
    const client::YBTableName& table_name) {
  vector<string> tablet_ids, ranges;
  RETURN_NOT_OK(yb_client_->GetTablets(table_name, 0, &tablet_ids, &ranges));
  master::GetTabletLocationsRequestPB req;
  for (const auto& tablet_id : tablet_ids) {
    req.add_tablet_ids(tablet_id);
  }
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClientProxy::GetTabletLocations, *master_client_proxy_, req));

  std::unordered_map<string, int> leader_counts;
  for (const auto& locs : resp.tablet_locations()) {
    for (const auto& replica : locs.replicas()) {
      const auto uuid = replica.ts_info().permanent_uuid();
      switch(replica.role()) {
        case PeerRole::LEADER:
          // If this is a leader, increment leader counts.
          ++leader_counts[uuid];
          break;
        case PeerRole::FOLLOWER:
          // If this is a follower, touch the leader count entry also so that tablet server with
          // followers only and 0 leader will be accounted for still.
          leader_counts[uuid];
          break;
        default:
          break;
      }
    }
  }

  return leader_counts;
}

Status ClusterAdminClient::SetupRedisTable() {
  const YBTableName table_name(
      YQL_DATABASE_REDIS, common::kRedisKeyspaceName, common::kRedisTableName);
  RETURN_NOT_OK(yb_client_->CreateNamespaceIfNotExists(common::kRedisKeyspaceName,
                                                       YQLDatabase::YQL_DATABASE_REDIS));
  // Try to create the table.
  std::unique_ptr<yb::client::YBTableCreator> table_creator(yb_client_->NewTableCreator());
  Status s = table_creator->table_name(table_name)
                              .table_type(yb::client::YBTableType::REDIS_TABLE_TYPE)
                              .Create();
  // If we could create it, then all good!
  if (s.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' created.";
    // If the table was already there, also not an error...
  } else if (s.IsAlreadyPresent()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' already exists";
  } else {
    // If any other error, report that!
    LOG(ERROR) << s;
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status ClusterAdminClient::DropRedisTable() {
  const YBTableName table_name(
      YQL_DATABASE_REDIS, common::kRedisKeyspaceName, common::kRedisTableName);
  Status s = yb_client_->DeleteTable(table_name, true /* wait */);
  if (s.ok()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' deleted.";
  } else if (s.IsNotFound()) {
    LOG(INFO) << "Table '" << table_name.ToString() << "' does not exist.";
  } else {
    RETURN_NOT_OK(s);
  }
  return Status::OK();
}

Status ClusterAdminClient::ChangeMasterConfig(
    const string& change_type,
    const string& peer_host,
    uint16_t peer_port,
    const string& given_uuid) {
  CHECK(initted_);

  consensus::ChangeConfigType cc_type;
  RETURN_NOT_OK(ParseChangeType(change_type, &cc_type));

  string peer_uuid;
  if (cc_type == consensus::ADD_SERVER) {
    VLOG(1) << "ChangeMasterConfig: attempt to get UUID for changed host: " << peer_host << ":"
            << peer_port;
    RETURN_NOT_OK(yb_client_->GetMasterUUID(peer_host, peer_port, &peer_uuid));
    if (!given_uuid.empty() && given_uuid != peer_uuid) {
      return STATUS_FORMAT(
          InvalidArgument, "Specified uuid $0. But the server has uuid $1", given_uuid, peer_uuid);
    }
  } else {
    // Do not verify uuid for REMOVE_SERVER, as the server may not be accessible.
    peer_uuid = given_uuid;
  }
  VLOG(1) << "ChangeMasterConfig: " << change_type << " | " << peer_host << ":" << peer_port
          << " uuid : " << peer_uuid;

  auto leader_uuid = VERIFY_RESULT(GetMasterLeaderUuid());

  // If removing the leader master, then first make it step down and that
  // starts an election and gets a new leader master.
  const HostPort changed_master_addr(peer_host, peer_port);
  if (cc_type == consensus::REMOVE_SERVER && leader_addr_ == changed_master_addr) {
    VLOG(1) << "ChangeMasterConfig: request leader " << leader_addr_
            << " to step down before removal.";
    string old_leader_uuid = leader_uuid;
    RETURN_NOT_OK(MasterLeaderStepDown(leader_uuid));
    sleep(5);  // TODO - wait for exactly the time needed for new leader to get elected.
    // Reget the leader master's socket info to set up the proxy
    ResetMasterProxy(VERIFY_RESULT(yb_client_->RefreshMasterLeaderAddress()));
    leader_uuid = VERIFY_RESULT(GetMasterLeaderUuid());
    if (leader_uuid == old_leader_uuid) {
      return STATUS(ConfigurationError,
        Substitute("Old master leader uuid $0 same as new one even after stepdown!", leader_uuid));
    }
    // Go ahead below and send the actual config change message to the new master
  }

  std::unique_ptr<consensus::ConsensusServiceProxy> leader_proxy(
    new consensus::ConsensusServiceProxy(proxy_cache_.get(), leader_addr_));
  consensus::ChangeConfigRequestPB req;

  RaftPeerPB peer_pb;
  if (!peer_uuid.empty()) {
    peer_pb.set_permanent_uuid(peer_uuid);
  }

  if (cc_type == consensus::ADD_SERVER) {
    peer_pb.set_member_type(consensus::PeerMemberType::PRE_VOTER);
  } else {  // REMOVE_SERVER
    req.set_use_host(peer_uuid.empty());
  }
  HostPortPB *peer_host_port = peer_pb.mutable_last_known_private_addr()->Add();
  peer_host_port->set_port(peer_port);
  peer_host_port->set_host(peer_host);
  req.set_dest_uuid(leader_uuid);
  req.set_tablet_id(yb::master::kSysCatalogTabletId);
  req.set_type(cc_type);
  *req.mutable_server() = peer_pb;

  VLOG(1) << "ChangeMasterConfig: ChangeConfig for tablet id " << yb::master::kSysCatalogTabletId
          << " to host " << leader_addr_;
  RETURN_NOT_OK(InvokeRpc(&consensus::ConsensusServiceProxy::ChangeConfig, *leader_proxy, req));

  VLOG(1) << "ChangeMasterConfig: update yb client to reflect config change.";
  if (cc_type == consensus::ADD_SERVER) {
    RETURN_NOT_OK(yb_client_->AddMasterToClient(changed_master_addr));
  } else {
    RETURN_NOT_OK(yb_client_->RemoveMasterFromClient(changed_master_addr));
  }

  return Status::OK();
}

Status ClusterAdminClient::GetTabletLocations(const TabletId& tablet_id,
                                              TabletLocationsPB* locations) {
  master::GetTabletLocationsRequestPB req;
  req.add_tablet_ids(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClientProxy::GetTabletLocations, *master_client_proxy_, req));

  if (resp.errors_size() > 0) {
    // This tool only needs to support one-by-one requests for tablet
    // locations, so we only look at the first error.
    return StatusFromPB(resp.errors(0).status());
  }

  // Same as above, no batching, and we already got past the error checks.
  CHECK_EQ(1, resp.tablet_locations_size()) << resp.ShortDebugString();

  *locations = resp.tablet_locations(0);
  return Status::OK();
}

Status ClusterAdminClient::GetTabletPeer(const TabletId& tablet_id,
                                         PeerMode mode,
                                         TSInfoPB* ts_info) {
  TabletLocationsPB locations;
  RETURN_NOT_OK(GetTabletLocations(tablet_id, &locations));
  CHECK_EQ(tablet_id, locations.tablet_id()) << locations.ShortDebugString();
  bool found = false;
  for (const TabletLocationsPB::ReplicaPB& replica : locations.replicas()) {
    if (mode == LEADER && replica.role() == PeerRole::LEADER) {
      *ts_info = replica.ts_info();
      found = true;
      break;
    }
    if (mode == FOLLOWER && replica.role() != PeerRole::LEADER) {
      *ts_info = replica.ts_info();
      found = true;
      break;
    }
  }

  if (!found) {
    return STATUS(NotFound,
      Substitute("No peer replica found in $0 mode for tablet $1", mode, tablet_id));
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTabletServers(
    RepeatedPtrField<ListTabletServersResponsePB::Entry>* servers) {
  auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::ListTabletServers, *master_cluster_proxy_,
      ListTabletServersRequestPB()));
  *servers = std::move(*resp.mutable_servers());
  return Status::OK();
}

Result<HostPort> ClusterAdminClient::GetFirstRpcAddressForTS(const PeerId& uuid) {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (server.instance_id().permanent_uuid() == uuid) {
      if (!server.has_registration() ||
          server.registration().common().private_rpc_addresses().empty()) {
        break;
      }
      return HostPortFromPB(server.registration().common().private_rpc_addresses(0));
    }
  }

  return STATUS_FORMAT(
      NotFound, "Server with UUID $0 has no RPC address registered with the Master", uuid);
}

Status ClusterAdminClient::ListAllTabletServers(bool exclude_dead) {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  char kSpaceSep = ' ';

  cout << RightPadToUuidWidth("Tablet Server UUID") << kSpaceSep
        << kRpcHostPortHeading << kSpaceSep
        << RightPadToWidth("Heartbeat delay", kLongColWidth) << kSpaceSep
        << RightPadToWidth("Status", kSmallColWidth) << kSpaceSep
        << RightPadToWidth("Reads/s", kSmallColWidth) << kSpaceSep
        << RightPadToWidth("Writes/s", kSmallColWidth) << kSpaceSep
        << RightPadToWidth("Uptime", kSmallColWidth) << kSpaceSep
        << RightPadToWidth("SST total size", kLongColWidth) << kSpaceSep
        << RightPadToWidth("SST uncomp size", kLongColWidth) << kSpaceSep
        << RightPadToWidth("SST #files", kLongColWidth) << kSpaceSep
        << RightPadToWidth("Memory", kSmallColWidth) << kSpaceSep
        << kBroadcastHeading << kSpaceSep
        << endl;
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (exclude_dead && server.has_alive() && !server.alive()) {
      continue;
    }
    std::stringstream time_str;
    auto heartbeat_delay_ms = server.has_millis_since_heartbeat() ?
                               server.millis_since_heartbeat() : 0;
    time_str << std::fixed << std::setprecision(2) << (heartbeat_delay_ms/1000.0) << "s";
    auto status_str = server.has_alive() ? (server.alive() ? "ALIVE" : "DEAD") : "UNKNOWN";
    cout << server.instance_id().permanent_uuid() << kSpaceSep
         << FormatFirstHostPort(server.registration().common().private_rpc_addresses())
         << kSpaceSep
         << RightPadToWidth(time_str.str(), kLongColWidth) << kSpaceSep
         << RightPadToWidth(status_str, kSmallColWidth) << kSpaceSep
         << RightPadToWidth(FormatDouble(server.metrics().read_ops_per_sec()), kSmallColWidth)
         << kSpaceSep
         << RightPadToWidth(FormatDouble(server.metrics().write_ops_per_sec()), kSmallColWidth)
         << kSpaceSep
         << RightPadToWidth(server.metrics().uptime_seconds(), kSmallColWidth) << kSpaceSep
         << RightPadToWidth(HumanizeBytes(server.metrics().total_sst_file_size()), kLongColWidth)
         << kSpaceSep
         << RightPadToWidth(HumanizeBytes(server.metrics().uncompressed_sst_file_size()),
                            kLongColWidth)
         << kSpaceSep
         << RightPadToWidth(server.metrics().num_sst_files(), kLongColWidth) << kSpaceSep
         << RightPadToWidth(HumanizeBytes(server.metrics().total_ram_usage()), kSmallColWidth)
         << kSpaceSep
         << FormatFirstHostPort(server.registration().common().broadcast_addresses())
         << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListAllMasters() {
  const auto lresp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::ListMasters, *master_cluster_proxy_,
      ListMastersRequestPB()));

  if (lresp.has_error()) {
    LOG(ERROR) << "Error: querying leader master for live master info : "
               << lresp.error().DebugString() << endl;
    return STATUS(RemoteError, lresp.error().DebugString());
  }

  cout << RightPadToUuidWidth("Master UUID") << kColumnSep
        << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth) << kColumnSep
        << RightPadToWidth("State", kSmallColWidth) << kColumnSep
        << "Role" << kColumnSep << RightPadToWidth(kBroadcastHeading, kHostPortColWidth) << endl;

  for (const auto& master : lresp.masters()) {
      const auto master_reg = master.has_registration() ? &master.registration() : nullptr;
      cout << (master.has_instance_id() ? master.instance_id().permanent_uuid()
                          : RightPadToUuidWidth("UNKNOWN_UUID")) << kColumnSep;
      cout << RightPadToWidth(
                master_reg ? FormatFirstHostPort(master_reg->private_rpc_addresses())
                            : "UNKNOWN", kHostPortColWidth)
            << kColumnSep;
      cout << RightPadToWidth((master.has_error() ?
                                PBEnumToString(master.error().code()) : "ALIVE"),
                              kSmallColWidth)
            << kColumnSep;
      cout << (master.has_role() ? PBEnumToString(master.role()) : "UNKNOWN") << kColumnSep;
      cout << RightPadToWidth(
        master_reg ? FormatFirstHostPort(master_reg->broadcast_addresses()) : "UNKNOWN",
        kHostPortColWidth) << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTabletServersLogLocations() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));

  if (!servers.empty()) {
    cout << RightPadToUuidWidth("TS UUID") << kColumnSep
         << kRpcHostPortHeading << kColumnSep
         << "LogLocation"
         << endl;
  }

  for (const ListTabletServersResponsePB::Entry& server : servers) {
    auto ts_uuid = server.instance_id().permanent_uuid();

    HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS(ts_uuid));

    TabletServerServiceProxy ts_proxy(proxy_cache_.get(), ts_addr);

    const auto resp = VERIFY_RESULT(InvokeRpc(
        &TabletServerServiceProxy::GetLogLocation, ts_proxy, tserver::GetLogLocationRequestPB()));
    cout << ts_uuid << kColumnSep
         << ts_addr << kColumnSep
         << resp.log_location() << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::ListTables(bool include_db_type,
                                      bool include_table_id,
                                      bool include_table_type) {
  const auto tables = VERIFY_RESULT(yb_client_->ListTables());
  const auto& namespace_metadata = VERIFY_RESULT_REF(GetNamespaceMap());
  vector<string> names;
  for (const auto& table : tables) {
    std::stringstream str;
    if (include_db_type) {
      const auto db_type_iter = namespace_metadata.find(table.namespace_id());
      if (db_type_iter != namespace_metadata.end()) {
        str << DatabasePrefix(db_type_iter->second.id.database_type()) << '.';
      } else {
        LOG(WARNING) << "Table in unknown namespace found " << table.ToString();
        continue;
      }
    }
    str << table.ToString();
    if (include_table_id) {
      str << ' ' << table.table_id();
    }
    if (include_table_type) {
      boost::optional<master::RelationType> relation_type = table.relation_type();
      switch (relation_type.get()) {
        case master::SYSTEM_TABLE_RELATION:
          str << " catalog";
          break;
        case master::USER_TABLE_RELATION:
          str << " table";
          break;
        case master::INDEX_TABLE_RELATION:
          str << " index";
          break;
        case master::MATVIEW_TABLE_RELATION:
          str << " matview";
          break;
        case master::COLOCATED_PARENT_TABLE_RELATION:
          str << " parent_table";
          break;
        default:
          str << " other";
      }
    }
    names.push_back(str.str());
  }
  sort(names.begin(), names.end());
  copy(names.begin(), names.end(), std::ostream_iterator<string>(cout, "\n"));
  return Status::OK();
}

struct FollowerDetails {
  string uuid;
  string host_port;
  string peer_role;
  FollowerDetails(const string &u, const string &hp, const string &role) :
    uuid(u), host_port(hp), peer_role(role) {}
};

Status ClusterAdminClient::ListTablets(
    const YBTableName& table_name, int max_tablets, bool json, bool followers) {
  vector<string> tablet_uuids, ranges;
  std::vector<master::TabletLocationsPB> locations;
  RETURN_NOT_OK(yb_client_->GetTablets(
      table_name, max_tablets, &tablet_uuids, &ranges, &locations));

  rapidjson::Document document(rapidjson::kObjectType);
  rapidjson::Value json_tablets(rapidjson::kArrayType);
  CHECK(json_tablets.IsArray());

  if (!json) {
    cout << RightPadToUuidWidth("Tablet-UUID") << kColumnSep
         << RightPadToWidth("Range", kPartitionRangeColWidth) << kColumnSep
         << RightPadToWidth("Leader-IP", kLongColWidth) << kColumnSep << "Leader-UUID";
    if (followers) {
      cout << kColumnSep << "Followers";
    }
    cout << endl;
  }

  for (size_t i = 0; i < tablet_uuids.size(); i++) {
    const string& tablet_uuid = tablet_uuids[i];
    string leader_host_port;
    string leader_uuid;
    string follower_host_port;
    vector<FollowerDetails> follower_list;
    string follower_list_str;
    const auto& locations_of_this_tablet = locations[i];
    for (const auto& replica : locations_of_this_tablet.replicas()) {
      if (replica.role() == PeerRole::LEADER) {
        if (leader_host_port.empty()) {
          leader_host_port = HostPortPBToString(replica.ts_info().private_rpc_addresses(0));
          leader_uuid = replica.ts_info().permanent_uuid();
        } else {
          LOG(ERROR) << "Multiple leader replicas found for tablet " << tablet_uuid
                     << ": " << locations_of_this_tablet.ShortDebugString();
        }
      } else {
        if (followers) {
          string follower_host_port =
            HostPortPBToString(replica.ts_info().private_rpc_addresses(0));
          if (json) {
            follower_list.push_back(
                FollowerDetails(replica.ts_info().permanent_uuid(), follower_host_port,
                  PeerRole_Name(replica.role())));
          } else {
            if (!follower_list_str.empty()) {
              follower_list_str += ",";
            }
            follower_list_str += follower_host_port;
          }
        }
      }
    }

    if (json) {
      rapidjson::Value json_tablet(rapidjson::kObjectType);
      AddStringField("id", tablet_uuid, &json_tablet, &document.GetAllocator());
      const auto& partition = locations_of_this_tablet.partition();
      AddStringField("partition_key_start",
                     Slice(partition.partition_key_start()).ToDebugHexString(), &json_tablet,
                     &document.GetAllocator());
      AddStringField("partition_key_end",
                     Slice(partition.partition_key_end()).ToDebugHexString(), &json_tablet,
                     &document.GetAllocator());
      rapidjson::Value json_leader(rapidjson::kObjectType);
      AddStringField("uuid", leader_uuid, &json_leader, &document.GetAllocator());
      AddStringField("endpoint", leader_host_port, &json_leader, &document.GetAllocator());
      AddStringField("role", PeerRole_Name(PeerRole::LEADER), &json_leader,
          &document.GetAllocator());
      json_tablet.AddMember(rapidjson::StringRef("leader"), json_leader, document.GetAllocator());
      if (followers) {
        rapidjson::Value json_followers(rapidjson::kArrayType);
        CHECK(json_followers.IsArray());
        for (const FollowerDetails &follower : follower_list) {
          rapidjson::Value json_follower(rapidjson::kObjectType);
          AddStringField("uuid", follower.uuid, &json_follower, &document.GetAllocator());
          AddStringField("endpoint", follower.host_port, &json_follower, &document.GetAllocator());
          AddStringField("role", follower.peer_role, &json_follower, &document.GetAllocator());
          json_followers.PushBack(json_follower, document.GetAllocator());
        }
        json_tablet.AddMember(rapidjson::StringRef("followers"), json_followers,
                              document.GetAllocator());
      }
      json_tablets.PushBack(json_tablet, document.GetAllocator());
    } else {
      cout << tablet_uuid << kColumnSep << RightPadToWidth(ranges[i], kPartitionRangeColWidth)
           << kColumnSep << RightPadToWidth(leader_host_port, kLongColWidth) << kColumnSep
           << leader_uuid;
      if (followers) {
        cout << kColumnSep << follower_list_str;
      }
      cout << endl;
    }
  }

  if (json) {
    document.AddMember("tablets", json_tablets, document.GetAllocator());
    std::cout << common::PrettyWriteRapidJsonToString(document) << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::LaunchBackfillIndexForTable(const YBTableName& table_name) {
  master::LaunchBackfillIndexForTableRequestPB req;
  table_name.SetIntoTableIdentifierPB(req.mutable_table_identifier());
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterDdlProxy::LaunchBackfillIndexForTable, *master_ddl_proxy_, req));
  if (resp.has_error()) {
    return STATUS(RemoteError, resp.error().DebugString());
  }
  return Status::OK();
}

Status ClusterAdminClient::ListPerTabletTabletServers(const TabletId& tablet_id) {
  master::GetTabletLocationsRequestPB req;
  req.add_tablet_ids(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClientProxy::GetTabletLocations, *master_client_proxy_, req));

  if (resp.tablet_locations_size() != 1) {
    if (resp.tablet_locations_size() > 0) {
      std::cerr << "List of all incorrect locations - " << resp.tablet_locations_size()
                << " : " << endl;
      const auto limit = std::min(resp.tablet_locations_size(), MAX_NUM_ELEMENTS_TO_SHOW_ON_ERROR);
      for (int i = 0; i < limit; ++i) {
        std::cerr << i << " : " << resp.tablet_locations(i).DebugString();
      }
      std::cerr << endl;
    }
    return STATUS_FORMAT(IllegalState,
                         "Incorrect number of locations $0 for tablet $1.",
                         resp.tablet_locations_size(), tablet_id);
  }

  TabletLocationsPB locs = resp.tablet_locations(0);
  if (!locs.replicas().empty()) {
    cout << RightPadToUuidWidth("Server UUID") << kColumnSep
         << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth) << kColumnSep
         << "Role" << endl;
  }
  for (const auto& replica : locs.replicas()) {
    cout << replica.ts_info().permanent_uuid() << kColumnSep
         << RightPadToWidth(HostPortPBToString(replica.ts_info().private_rpc_addresses(0)),
                            kHostPortColWidth) << kColumnSep
         << PBEnumToString(replica.role()) << endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::DeleteTable(const YBTableName& table_name) {
  RETURN_NOT_OK(yb_client_->DeleteTable(table_name));
  cout << "Deleted table " << table_name.ToString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteTableById(const TableId& table_id) {
  RETURN_NOT_OK(yb_client_->DeleteTable(table_id));
  cout << "Deleted table " << table_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteIndex(const YBTableName& table_name) {
  YBTableName indexed_table_name;
  RETURN_NOT_OK(yb_client_->DeleteIndexTable(table_name, &indexed_table_name));
  cout << "Deleted index " << table_name.ToString() << " from table " <<
      indexed_table_name.ToString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteIndexById(const TableId& table_id) {
  YBTableName indexed_table_name;
  RETURN_NOT_OK(yb_client_->DeleteIndexTable(table_id, &indexed_table_name));
  cout << "Deleted index " << table_id << " from table " <<
      indexed_table_name.ToString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListAllNamespaces() {
  cout << "name | UUID | language | state | colocated" << endl << endl;
  const auto namespaces = VERIFY_RESULT_REF(GetNamespaceMap());
  const auto list_namespaces = [&] (bool for_system_namespace) -> void {
    cout << (for_system_namespace ? "System Namespaces:" : "User Namespaces:") << endl;
    for (const auto& namespace_info_pair : namespaces) {
      const auto namespace_info = namespace_info_pair.second;
      if (for_system_namespace == master::IsSystemNamespace(namespace_info.id.name())) {
        cout << namespace_info.id.name()
        << " " << namespace_info.id.id()
        << " " << DatabasePrefix(namespace_info.id.database_type())
        << " " << SysNamespaceEntryPB_State_Name(namespace_info.state)
        << " " << (namespace_info.colocated ? "true" : "false")
        << endl;
      }
    }
  };

  list_namespaces(false /* for_system_namespace */);
  cout << endl;
  list_namespaces(true /* for_system_namespace */);
  return Status::OK();
}

Status ClusterAdminClient::DeleteNamespace(const TypedNamespaceName& namespace_name) {
  RETURN_NOT_OK(yb_client_->DeleteNamespace(namespace_name.name, namespace_name.db_type));
  cout << "Deleted namespace " << namespace_name.name << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteNamespaceById(const NamespaceId& namespace_id) {
  RETURN_NOT_OK(yb_client_->DeleteNamespace(
      std::string() /* name */, boost::none /* database type */, namespace_id));
  cout << "Deleted namespace " << namespace_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListTabletsForTabletServer(const PeerId& ts_uuid) {
  auto ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS(ts_uuid));

  TabletServerServiceProxy ts_proxy(proxy_cache_.get(), ts_addr);

  const auto resp = VERIFY_RESULT(InvokeRpc(
      &TabletServerServiceProxy::ListTabletsForTabletServer, ts_proxy,
      tserver::ListTabletsForTabletServerRequestPB()));

  cout << RightPadToWidth("Table name", kTableNameColWidth) << kColumnSep
       << RightPadToUuidWidth("Tablet ID") << kColumnSep
       << "Is Leader" << kColumnSep
       << "State" << kColumnSep
       << "Num SST Files" << kColumnSep
       << "Num Log Segments" << kColumnSep
       << "Num Memtables (Intents/Regular)" << endl;
  for (const auto& entry : resp.entries()) {
    cout << RightPadToWidth(entry.table_name(), kTableNameColWidth) << kColumnSep
         << RightPadToUuidWidth(entry.tablet_id()) << kColumnSep
         << entry.is_leader() << kColumnSep
         << PBEnumToString(entry.state()) << kColumnSep
         << entry.num_sst_files() << kColumnSep
         << entry.num_log_segments() << kColumnSep
         << entry.num_memtables_intents() << "/" << entry.num_memtables_regular() << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::SetLoadBalancerEnabled(bool is_enabled) {
  const auto list_resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::ListMasters, *master_cluster_proxy_,
      ListMastersRequestPB()));

  master::ChangeLoadBalancerStateRequestPB req;
  req.set_is_enabled(is_enabled);
  for (const auto& master : list_resp.masters()) {

    if (master.role() == PeerRole::LEADER) {
      RETURN_NOT_OK(InvokeRpc(
          &master::MasterClusterProxy::ChangeLoadBalancerState, *master_cluster_proxy_,
          req));
    } else {
      HostPortPB hp_pb = master.registration().private_rpc_addresses(0);

      master::MasterClusterProxy proxy(proxy_cache_.get(), HostPortFromPB(hp_pb));
      RETURN_NOT_OK(InvokeRpc(
          &master::MasterClusterProxy::ChangeLoadBalancerState, proxy, req));
    }
  }

  return Status::OK();
}

Status ClusterAdminClient::GetLoadBalancerState() {
  const auto list_resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterClusterProxy::ListMasters, *master_cluster_proxy_,
      ListMastersRequestPB()));

  if (list_resp.has_error()) {
    LOG(ERROR) << "Error: querying leader master for live master info : "
               << list_resp.error().DebugString() << endl;
    return STATUS(RemoteError, list_resp.error().DebugString());
  }

  cout << RightPadToUuidWidth("Master UUID") << kColumnSep
       << RightPadToWidth(kRpcHostPortHeading, kHostPortColWidth) << kColumnSep
       << RightPadToWidth("State", kSmallColWidth) << kColumnSep
       << RightPadToWidth("Role", kSmallColWidth) << kColumnSep
       << "Load Balancer State" << endl;


  master::GetLoadBalancerStateRequestPB req;
  master::GetLoadBalancerStateResponsePB resp;
  string error;
  master::MasterClusterProxy* proxy;
  for (const auto& master : list_resp.masters()) {
    error.clear();
    std::unique_ptr<master::MasterClusterProxy> follower_proxy;
    if (master.role() == PeerRole::LEADER) {
      proxy = master_cluster_proxy_.get();
    } else {
      HostPortPB hp_pb = master.registration().private_rpc_addresses(0);
      follower_proxy = std::make_unique<master::MasterClusterProxy>(
          proxy_cache_.get(), HostPortFromPB(hp_pb));
      proxy = follower_proxy.get();
    }
    auto result = InvokeRpc(&master::MasterClusterProxy::GetLoadBalancerState, *proxy, req);
    if (!result) {
      error = result.ToString();
    } else {
      resp = *result;
      if (!resp.has_error()) {
        error = resp.error().status().message();
      }
    }
    const auto master_reg = master.has_registration() ? &master.registration() : nullptr;
    cout << (master.has_instance_id() ? master.instance_id().permanent_uuid()
                                      : RightPadToUuidWidth("UNKNOWN_UUID")) << kColumnSep;
    cout << RightPadToWidth(
        master_reg ? FormatFirstHostPort(master_reg->private_rpc_addresses())
                   : "UNKNOWN", kHostPortColWidth)
         << kColumnSep;
    cout << RightPadToWidth((master.has_error() ?
                             PBEnumToString(master.error().code()) : "ALIVE"), kSmallColWidth)
         << kColumnSep;
    cout << RightPadToWidth((master.has_role() ?
                             PBEnumToString(master.role()) : "UNKNOWN"), kSmallColWidth)
         << kColumnSep;
    cout << (!error.empty() ? "Error: " + error : (resp.is_enabled() ? "ENABLED" : "DISABLED"))
         << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::FlushTables(const std::vector<YBTableName>& table_names,
                                       bool add_indexes,
                                       int timeout_secs,
                                       bool is_compaction) {
  RETURN_NOT_OK(yb_client_->FlushTables(table_names, add_indexes, timeout_secs, is_compaction));
  cout << (is_compaction ? "Compacted " : "Flushed ")
       << yb::ToString(table_names) << " tables"
       << (add_indexes ? " and associated indexes." : ".") << endl;
  return Status::OK();
}

Status ClusterAdminClient::FlushTablesById(
    const std::vector<TableId>& table_ids,
    bool add_indexes,
    int timeout_secs,
    bool is_compaction) {
  RETURN_NOT_OK(yb_client_->FlushTables(table_ids, add_indexes, timeout_secs, is_compaction));
  cout << (is_compaction ? "Compacted " : "Flushed ")
       << yb::ToString(table_ids) << " tables"
       << (add_indexes ? " and associated indexes." : ".") << endl;
  return Status::OK();
}

Status ClusterAdminClient::CompactionStatus(const YBTableName& table_name, bool show_tablets) {
  const auto compaction_status =
      VERIFY_RESULT(yb_client_->GetCompactionStatus(table_name, show_tablets));

  const auto ShowTablets = [&compaction_status]() {
    std::map<TabletServerId, std::vector<client::TabletReplicaFullCompactionStatus>>
        replica_statuses;
    for (const auto& replica_status : compaction_status.replica_statuses) {
      replica_statuses[replica_status.ts_id].push_back(replica_status);
    }

    for (const auto& [ts_id, statuses] : replica_statuses) {
      cout << "tserver uuid: " << ts_id << endl
           << " tablet id | full compaction state | last full compaction completion time" << endl
           << endl;
      for (const auto& status : statuses) {
        cout << " " << status.tablet_id << " ";
        if (status.full_compaction_state == tablet::FULL_COMPACTION_STATE_UNKNOWN) {
          cout << "UNKNOWN" << endl;
          continue;
        }
        cout << tablet::FullCompactionState_Name(status.full_compaction_state) << " ";
        if (status.last_full_compaction_time.ToUint64() == 0) {
          cout << "never been full compacted" << endl;
        } else {
          cout << HybridTimeToString(status.last_full_compaction_time) << endl;
        }
      }
      cout << endl;
    }
  };

  switch (compaction_status.full_compaction_state) {
    case tablet::FULL_COMPACTION_STATE_UNKNOWN:
      cout << "Compaction status unavailable. Waiting for heartbeats" << endl;
      break;
    case tablet::COMPACTING:
      cout << "A full compaction is currently ongoing" << endl;
      break;
    case tablet::IDLE:
      cout << "No full compaction taking place" << endl;
      break;
  }

  if (show_tablets) {
    cout << endl;
    ShowTablets();
  }

  if (compaction_status.full_compaction_state == tablet::COMPACTING ||
      compaction_status.full_compaction_state == tablet::IDLE) {
    if (compaction_status.last_full_compaction_time.ToUint64() == 0) {
      cout << "A full compaction has never been completed" << endl;
    } else {
      cout << "Last full compaction completion time: "
           << HybridTimeToString(compaction_status.last_full_compaction_time) << endl;
    }
  }

  if (compaction_status.last_request_time.ToUint64() == 0) {
    cout << "An admin compaction has never been requested" << endl;
  } else {
    cout << "Last admin compaction request time: "
         << HybridTimeToString(compaction_status.last_request_time) << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::FlushSysCatalog() {
  master::FlushSysCatalogRequestPB req;
  auto res = InvokeRpc(
      &master::MasterAdminProxy::FlushSysCatalog, *master_admin_proxy_, req);
  return res.ok() ? Status::OK() : res.status();
}

Status ClusterAdminClient::CompactSysCatalog() {
  master::CompactSysCatalogRequestPB req;
  auto res = InvokeRpc(
      &master::MasterAdminProxy::CompactSysCatalog, *master_admin_proxy_, req);
  return res.ok() ? Status::OK() : res.status();
}

Status ClusterAdminClient::WaitUntilMasterLeaderReady() {
  for(int iter = 0; iter < kNumberOfTryouts; ++iter) {
    const auto res_leader_ready = VERIFY_RESULT(InvokeRpcNoResponseCheck(
        &master::MasterClusterProxy::IsMasterLeaderServiceReady,
        *master_cluster_proxy_,  master::IsMasterLeaderReadyRequestPB(),
        "MasterServiceImpl::IsMasterLeaderServiceReady call failed."));
    if (!res_leader_ready.has_error()) {
      return Status::OK();
    }
    sleep(kSleepTimeSec);
  }
  return STATUS(TimedOut, "ClusterAdminClient::WaitUntilMasterLeaderReady timed out.");
}

Status ClusterAdminClient::AddReadReplicaPlacementInfo(
    const string& placement_info, int replication_factor, const std::string& optional_uuid) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto resp_cluster_config = VERIFY_RESULT(GetMasterClusterConfig());

  auto* cluster_config = resp_cluster_config.mutable_cluster_config();
  if (cluster_config->replication_info().read_replicas_size() > 0) {
    return STATUS(InvalidCommand, "Already have a read replica placement, cannot add another.");
  }
  auto* read_replica_config = cluster_config->mutable_replication_info()->add_read_replicas();

  // If optional_uuid is set, make that the placement info, otherwise generate a random one.
  string uuid_str = optional_uuid;
  if (optional_uuid.empty()) {
    uuid_str = RandomHumanReadableString(16);
  }
  read_replica_config->set_num_replicas(replication_factor);
  read_replica_config->set_placement_uuid(uuid_str);

  // Fill in the placement info with new stuff.
  RETURN_NOT_OK(FillPlacementInfo(read_replica_config, placement_info));

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&master::MasterClusterProxy::ChangeMasterClusterConfig,
                          *master_cluster_proxy_, req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO) << "Created read replica placement with uuid: " << uuid_str;
  return Status::OK();
}

Status ClusterAdminClient::ModifyReadReplicaPlacementInfo(
    const std::string& placement_uuid, const std::string& placement_info, int replication_factor) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto master_resp = VERIFY_RESULT(GetMasterClusterConfig());
  auto* cluster_config = master_resp.mutable_cluster_config();

  auto* replication_info = cluster_config->mutable_replication_info();
  if (replication_info->read_replicas_size() == 0) {
    return STATUS(InvalidCommand, "No read replica placement info to modify.");
  }

  auto* read_replica_config = replication_info->mutable_read_replicas(0);

  std::string config_placement_uuid;
  if (placement_uuid.empty())  {
    // If there is no placement_uuid set, use the existing uuid.
    config_placement_uuid = read_replica_config->placement_uuid();
  } else {
    // Otherwise, use the passed in value.
    config_placement_uuid = placement_uuid;
  }

  read_replica_config->Clear();

  read_replica_config->set_num_replicas(replication_factor);
  read_replica_config->set_placement_uuid(config_placement_uuid);
  RETURN_NOT_OK(FillPlacementInfo(read_replica_config, placement_info));

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&master::MasterClusterProxy::ChangeMasterClusterConfig,
                          *master_cluster_proxy_, req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO) << "Changed read replica placement.";
  return Status::OK();
}

Status ClusterAdminClient::DeleteReadReplicaPlacementInfo() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  auto master_resp = VERIFY_RESULT(GetMasterClusterConfig());
  auto* cluster_config = master_resp.mutable_cluster_config();

  auto* replication_info = cluster_config->mutable_replication_info();
  if (replication_info->read_replicas_size() == 0) {
    return STATUS(InvalidCommand, "No read replica placement info to delete.");
  }

  replication_info->clear_read_replicas();

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;

  *req_new_cluster_config.mutable_cluster_config() = *cluster_config;

  RETURN_NOT_OK(InvokeRpc(&master::MasterClusterProxy::ChangeMasterClusterConfig,
                          *master_cluster_proxy_, req_new_cluster_config,
                          "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO) << "Deleted read replica placement.";
  return Status::OK();
}

Status ClusterAdminClient::FillPlacementInfo(
    master::PlacementInfoPB* placement_info_pb, const string& placement_str) {
  placement_info_pb->clear_placement_blocks();

  std::vector<std::string> placement_info_splits = strings::Split(
      placement_str, ",", strings::AllowEmpty());

  std::unordered_map<std::string, int> placement_to_min_replicas;
  for (auto& placement_info_split : placement_info_splits) {
    std::vector<std::string> placement_block_split =
        strings::Split(placement_info_split, ":", strings::AllowEmpty());

    if (placement_block_split.size() == 0 || placement_block_split.size() > 2) {
      return STATUS(
          InvalidCommand,
          "Each placement block must be of the form 'cloud.region.zone:[min_replica_count]'. "
          "Invalid placement block: " + placement_info_split);
    }

    int min_replicas = 1;
    if (placement_block_split.size() == 2) {
      min_replicas = VERIFY_RESULT(CheckedStoi(placement_block_split[1]));
    }
    placement_to_min_replicas[placement_block_split[0]] += min_replicas;
  }

  for (auto& [placement_block, min_replicas] : placement_to_min_replicas) {
    std::vector<std::string> blocks = strings::Split(placement_block, ".",
                                                    strings::AllowEmpty());
    auto* pb = placement_info_pb->add_placement_blocks();
    if (blocks.size() > 0 && !blocks[0].empty()) {
      pb->mutable_cloud_info()->set_placement_cloud(blocks[0]);
    }

    if (blocks.size() > 1 && !blocks[1].empty()) {
      pb->mutable_cloud_info()->set_placement_region(blocks[1]);
    }

    if (blocks.size() > 2 && !blocks[2].empty()) {
      pb->mutable_cloud_info()->set_placement_zone(blocks[2]);
    }

    pb->set_min_num_replicas(min_replicas);
  }

  return Status::OK();
}

Status ClusterAdminClient::ModifyTablePlacementInfo(
  const YBTableName& table_name, const std::string& placement_info, int replication_factor,
  const std::string& optional_uuid) {

  YBTableName global_transactions(
      YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  if (table_name == global_transactions) {
    return STATUS(InvalidCommand, "Placement cannot be modified for the global transactions table");
  }

  master::PlacementInfoPB live_replicas;
  live_replicas.set_num_replicas(replication_factor);
  RETURN_NOT_OK(FillPlacementInfo(&live_replicas, placement_info));

  if (!optional_uuid.empty()) {
    // If we have a placement uuid, set it.
    live_replicas.set_placement_uuid(optional_uuid);
  }

  return yb_client_->ModifyTablePlacementInfo(table_name, std::move(live_replicas));
}

Status ClusterAdminClient::ModifyPlacementInfo(
    std::string placement_info, int replication_factor, const std::string& optional_uuid) {

  // Wait to make sure that master leader is ready.
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto resp_cluster_config = VERIFY_RESULT(GetMasterClusterConfig());

  // Create a new cluster config.
  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;
  master::SysClusterConfigEntryPB* sys_cluster_config_entry =
      resp_cluster_config.mutable_cluster_config();
  master::PlacementInfoPB* live_replicas =
      sys_cluster_config_entry->mutable_replication_info()->mutable_live_replicas();
  live_replicas->set_num_replicas(replication_factor);
  RETURN_NOT_OK(FillPlacementInfo(live_replicas, placement_info));

  if (!optional_uuid.empty()) {
    // If we have an optional uuid, set it.
    live_replicas->set_placement_uuid(optional_uuid);
  } else if (sys_cluster_config_entry->replication_info().live_replicas().has_placement_uuid()) {
    // Otherwise, if we have an existing placement uuid, use that.
    live_replicas->set_placement_uuid(
        sys_cluster_config_entry->replication_info().live_replicas().placement_uuid());
  }

  req_new_cluster_config.mutable_cluster_config()->CopyFrom(*sys_cluster_config_entry);

  RETURN_NOT_OK(InvokeRpc(
      &master::MasterClusterProxy::ChangeMasterClusterConfig, *master_cluster_proxy_,
      req_new_cluster_config, "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO) << "Changed master cluster config.";
  return Status::OK();
}

Status ClusterAdminClient::ClearPlacementInfo() {
  // Wait to make sure that master leader is ready.
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");

  // Get the cluster config from the master leader.
  auto resp_cluster_config = VERIFY_RESULT(GetMasterClusterConfig());

  master::SysClusterConfigEntryPB* sys_cluster_config_entry =
      resp_cluster_config.mutable_cluster_config();
  sys_cluster_config_entry->clear_replication_info();

  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;
  req_new_cluster_config.mutable_cluster_config()->CopyFrom(*sys_cluster_config_entry);

  RETURN_NOT_OK(InvokeRpc(
      &master::MasterClusterProxy::ChangeMasterClusterConfig, *master_cluster_proxy_,
      req_new_cluster_config, "MasterServiceImpl::ChangeMasterClusterConfig call failed."));

  LOG(INFO) << "Cleared master placement info config";
  return Status::OK();
}

Status ClusterAdminClient::GetUniverseConfig() {
  const auto cluster_config = VERIFY_RESULT(GetMasterClusterConfig());
  std::string output;
  MessageToJsonString(cluster_config.cluster_config(), &output);
  cout << output << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetXClusterConfig() {
  const auto xcluster_config = VERIFY_RESULT(GetMasterXClusterConfig());
  const auto cluster_config = VERIFY_RESULT(GetMasterClusterConfig());
  std::string producer_registry_output;
  std::string consumer_registry_output;
  MessageToJsonString(
      xcluster_config.xcluster_config().xcluster_producer_registry(), &producer_registry_output);
  MessageToJsonString(
      cluster_config.cluster_config().consumer_registry(), &consumer_registry_output);
  cout << Format(
              "{\"version\":$0,\"xcluster_producer_registry\":$1,\"consumer_"
              "registry\":$2}",
              xcluster_config.xcluster_config().version(), producer_registry_output,
              consumer_registry_output)
       << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetYsqlCatalogVersion() {
  uint64_t version = 0;
  RETURN_NOT_OK(yb_client_->GetYsqlCatalogMasterVersion(&version));
  cout << "Version: "  << version << endl;
  return Status::OK();
}

Result<rapidjson::Document> ClusterAdminClient::DdlLog() {
  RpcController rpc;
  rpc.set_timeout(timeout_);
  master::DdlLogRequestPB req;
  master::DdlLogResponsePB resp;

  RETURN_NOT_OK(master_admin_proxy_->DdlLog(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  rapidjson::Document result;
  result.SetObject();
  rapidjson::Value json_entries(rapidjson::kArrayType);
  for (const auto& entry : resp.entries()) {
    rapidjson::Value json_entry(rapidjson::kObjectType);
    AddStringField("table_type", TableType_Name(entry.table_type()), &json_entry,
                   &result.GetAllocator());
    AddStringField("namespace", entry.namespace_name(), &json_entry, &result.GetAllocator());
    AddStringField("table", entry.table_name(), &json_entry, &result.GetAllocator());
    AddStringField("action", entry.action(), &json_entry, &result.GetAllocator());
    AddStringField("time", HybridTimeToString(HybridTime(entry.time())),
                   &json_entry, &result.GetAllocator());
    json_entries.PushBack(json_entry, result.GetAllocator());
  }
  result.AddMember("log", json_entries, result.GetAllocator());
  return result;
}

Status ClusterAdminClient::UpgradeYsql(bool use_single_connection) {
  {
    master::IsInitDbDoneRequestPB req;
    auto res = InvokeRpc(
        &master::MasterAdminProxy::IsInitDbDone, *master_admin_proxy_, req);
    if (!res.ok()) {
      return res.status();
    }
    if (!res->done()) {
      cout << "Upgrade is not needed since YSQL is disabled" << endl;
      return Status::OK();
    }
    if (res->done() && res->has_error()) {
      return STATUS_FORMAT(IllegalState,
                           "YSQL is not ready, initdb finished with an error: $0",
                           res->error());
    }
    // Otherwise, we can proceed.
  }

  // Pick some alive TServer.
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  boost::optional<HostPortPB> ts_rpc_addr;
  for (const ListTabletServersResponsePB::Entry& server : servers) {
    if (!server.has_alive() || !server.alive()) {
      continue;
    }

    if (!server.has_registration() ||
        server.registration().common().private_rpc_addresses().empty()) {
      continue;
    }

    ts_rpc_addr.emplace(server.registration().common().private_rpc_addresses(0));
    break;
  }
  if (!ts_rpc_addr.has_value()) {
    return STATUS(IllegalState, "Couldn't find alive tablet server to connect to");
  }

  TabletServerAdminServiceProxy ts_admin_proxy(proxy_cache_.get(), HostPortFromPB(*ts_rpc_addr));

  UpgradeYsqlRequestPB req;
  req.set_use_single_connection(use_single_connection);
  const auto resp_result = InvokeRpc(&TabletServerAdminServiceProxy::UpgradeYsql,
                                     ts_admin_proxy, req);
  if (!resp_result.ok()) {
    return resp_result.status();
  }
  if (resp_result->has_error()) {
    return StatusFromPB(resp_result->error().status());
  }

  cout << "YSQL successfully upgraded to the latest version" << endl;
  return Status::OK();
}

Status ClusterAdminClient::ChangeBlacklist(const std::vector<HostPort>& servers, bool add,
    bool blacklist_leader) {
  auto config = VERIFY_RESULT(GetMasterClusterConfig());
  auto& cluster_config = *config.mutable_cluster_config();
  auto& blacklist = (blacklist_leader) ?
    *cluster_config.mutable_leader_blacklist() :
    *cluster_config.mutable_server_blacklist();
  std::vector<HostPort> result_blacklist;
  for (const auto& host : blacklist.hosts()) {
    const HostPort hostport(host.host(), host.port());
    if (std::find(servers.begin(), servers.end(), hostport) == servers.end()) {
      result_blacklist.emplace_back(host.host(), host.port());
    }
  }
  if (add) {
    result_blacklist.insert(result_blacklist.end(), servers.begin(), servers.end());
  }
  auto result_begin = result_blacklist.begin(), result_end = result_blacklist.end();
  std::sort(result_begin, result_end);
  result_blacklist.erase(std::unique(result_begin, result_end), result_end);
  blacklist.clear_hosts();
  for (const auto& hostport : result_blacklist) {
    auto& new_host = *blacklist.add_hosts();
    new_host.set_host(hostport.host());
    new_host.set_port(hostport.port());
  }
  master::ChangeMasterClusterConfigRequestPB req_new_cluster_config;
  req_new_cluster_config.mutable_cluster_config()->Swap(&cluster_config);
  return ResultToStatus(InvokeRpc(&master::MasterClusterProxy::ChangeMasterClusterConfig,
                                  *master_cluster_proxy_, req_new_cluster_config,
                                  "MasterServiceImpl::ChangeMasterClusterConfig call failed."));
}

Result<const master::NamespaceIdentifierPB&> ClusterAdminClient::GetNamespaceInfo(
    YQLDatabase db_type, const std::string& namespace_name) {
  LOG(INFO) << Format(
      "Resolving namespace id for '$0' of type '$1'", namespace_name, DatabasePrefix(db_type));
  for (const auto& item : VERIFY_RESULT_REF(GetNamespaceMap())) {
    const auto& namespace_info = item.second;
    if (namespace_info.id.database_type() == db_type &&
        namespace_name == namespace_info.id.name()) {
      return namespace_info.id;
    }
  }
  return STATUS_FORMAT(
      NotFound, "Namespace '$0' of type '$1' not found", namespace_name, DatabasePrefix(db_type));
}

Result<master::GetMasterClusterConfigResponsePB> ClusterAdminClient::GetMasterClusterConfig() {
  return InvokeRpc(&master::MasterClusterProxy::GetMasterClusterConfig,
                   *master_cluster_proxy_, master::GetMasterClusterConfigRequestPB(),
                   "MasterServiceImpl::GetMasterClusterConfig call failed.");
}

Result<master::GetMasterXClusterConfigResponsePB> ClusterAdminClient::GetMasterXClusterConfig() {
  return InvokeRpc(&master::MasterClusterProxy::GetMasterXClusterConfig,
                   *master_cluster_proxy_, master::GetMasterXClusterConfigRequestPB(),
                   "MasterServiceImpl::GetMasterXClusterConfig call failed.");
}

Status ClusterAdminClient::SplitTablet(const std::string& tablet_id) {
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  const auto resp = VERIFY_RESULT(InvokeRpc(
      &master::MasterAdminProxy::SplitTablet, *master_admin_proxy_, req));
  if (resp.has_error()) {
    std::cout << "Response: " << AsString(resp);
  } else {
    std::cout << "split_tablet \"" << tablet_id << "\" was sent asynchronously. "
                 "Check master logs for more details about the status of the task.";
  }
  std::cout << std::endl;
  return Status::OK();
}

Result<master::DisableTabletSplittingResponsePB> ClusterAdminClient::DisableTabletSplitsInternal(
    int64_t disable_duration_ms, const std::string& feature_name) {
  master::DisableTabletSplittingRequestPB req;
  req.set_disable_duration_ms(disable_duration_ms);
  req.set_feature_name(feature_name);
  return InvokeRpc(&master::MasterAdminProxy::DisableTabletSplitting, *master_admin_proxy_, req);
}

Status ClusterAdminClient::DisableTabletSplitting(
    int64_t disable_duration_ms, const std::string& feature_name) {
  const auto resp = VERIFY_RESULT(DisableTabletSplitsInternal(disable_duration_ms, feature_name));
  std::cout << "Response: " << AsString(resp) << std::endl;
  return Status::OK();
}

Result<master::IsTabletSplittingCompleteResponsePB>
    ClusterAdminClient::IsTabletSplittingCompleteInternal(
    bool wait_for_parent_deletion, const MonoDelta timeout) {
  master::IsTabletSplittingCompleteRequestPB req;
  req.set_wait_for_parent_deletion(wait_for_parent_deletion);
  return InvokeRpc(&master::MasterAdminProxy::IsTabletSplittingComplete, *master_admin_proxy_, req,
      nullptr /* error_message */, timeout);
}

Status ClusterAdminClient::IsTabletSplittingComplete(bool wait_for_parent_deletion) {
  const auto resp = VERIFY_RESULT(IsTabletSplittingCompleteInternal(wait_for_parent_deletion));
  std::cout << "Response: " << AsString(resp) << std::endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateTransactionsStatusTable(const std::string& table_name) {
  return yb_client_->CreateTransactionsStatusTable(table_name);
}

Status ClusterAdminClient::AddTransactionStatusTablet(const TableId& table_id) {
  return yb_client_->AddTransactionStatusTablet(table_id);
}

template<class Response, class Request, class Object>
Result<Response> ClusterAdminClient::InvokeRpcNoResponseCheck(
    Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
    const Object& obj, const Request& req, const char* error_message, const MonoDelta timeout) {
  rpc::RpcController rpc;
  rpc.set_timeout(timeout.Initialized() ? timeout : timeout_);
  Response response;
  auto result = (obj.*func)(req, &response, &rpc);
  if (error_message) {
    RETURN_NOT_OK_PREPEND(result, error_message);
  } else {
    RETURN_NOT_OK(result);
  }
  return std::move(response);
}

template<class Response, class Request, class Object>
Result<Response> ClusterAdminClient::InvokeRpc(
    Status (Object::*func)(const Request&, Response*, rpc::RpcController*) const,
    const Object& obj, const Request& req, const char* error_message, const MonoDelta timeout) {
  return ResponseResult(
      VERIFY_RESULT(InvokeRpcNoResponseCheck(func, obj, req, error_message, timeout)));
}

Result<const ClusterAdminClient::NamespaceMap&> ClusterAdminClient::GetNamespaceMap() {
  if (namespace_map_.empty()) {
    auto v = VERIFY_RESULT(yb_client_->ListNamespaces());
    for (auto& ns : v) {
      auto ns_id = ns.id.id();
      namespace_map_.emplace(std::move(ns_id), std::move(ns));
    }
  }
  return const_cast<const ClusterAdminClient::NamespaceMap&>(namespace_map_);
}

Result<TableNameResolver> ClusterAdminClient::BuildTableNameResolver(
    TableNameResolver::Values* tables) {
      const auto namespaces = VERIFY_RESULT(yb_client_->ListNamespaces());
      vector<master::NamespaceIdentifierPB> namespace_ids;
      for (const auto& ns : namespaces) {
        namespace_ids.push_back(ns.id);
      }
  return TableNameResolver(
      tables, VERIFY_RESULT(yb_client_->ListTables()), std::move(namespace_ids));
}

Result<ListSnapshotsResponsePB> ClusterAdminClient::ListSnapshots(const ListSnapshotsFlags& flags) {
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListSnapshotsRequestPB req;
    req.set_list_deleted_snapshots(flags.Test(ListSnapshotsFlag::SHOW_DELETED));
    auto* flags_pb = req.mutable_detail_options();
    // Explicitly set all boolean fields as the defaults of this proto are inconsistent.
    if (flags.Test(ListSnapshotsFlag::SHOW_DETAILS)) {
      flags_pb->set_show_namespace_details(true);
      flags_pb->set_show_udtype_details(true);
      flags_pb->set_show_table_details(true);
      flags_pb->set_show_tablet_details(false);
    } else {
      flags_pb->set_show_namespace_details(false);
      flags_pb->set_show_udtype_details(false);
      flags_pb->set_show_table_details(false);
      flags_pb->set_show_tablet_details(false);
    }
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));
  return resp;
}

Status ClusterAdminClient::CreateSnapshot(
    const vector<YBTableName>& tables, std::optional<int32_t> retention_duration_hours,
    const bool add_indexes, const int flush_timeout_secs) {
  if (flush_timeout_secs > 0) {
        const auto status = FlushTables(tables, add_indexes, flush_timeout_secs, false);
        if (status.IsTimedOut()) {
      cout << status.ToString(false) << " (ignored)" << endl;
        } else if (!status.ok() && !status.IsNotFound()) {
      return status;
        }
  }

  CreateSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    CreateSnapshotRequestPB req;
    for (const YBTableName& table_name : tables) {
      table_name.SetIntoTableIdentifierPB(req.add_tables());
    }

    req.set_add_indexes(add_indexes);
    req.set_add_ud_types(true);  // No-op for YSQL.
    req.set_transaction_aware(true);
    if (retention_duration_hours && *retention_duration_hours <= 0) {
      req.set_retention_duration_hours(-1);
    } else if (retention_duration_hours) {
      req.set_retention_duration_hours(*retention_duration_hours);
    }
    return master_backup_proxy_->CreateSnapshot(req, &resp, rpc);
  }));

  cout << "Started snapshot creation: " << SnapshotIdToString(resp.snapshot_id()) << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateNamespaceSnapshot(
    const TypedNamespaceName& ns, std::optional<int32_t> retention_duration_hours,
    bool add_indexes) {
  ListTablesResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListTablesRequestPB req;

    req.mutable_namespace_()->set_name(ns.name);
    req.mutable_namespace_()->set_database_type(ns.db_type);
    req.set_exclude_system_tables(true);
    req.add_relation_type_filter(master::USER_TABLE_RELATION);
    if (add_indexes) {
      req.add_relation_type_filter(master::INDEX_TABLE_RELATION);
    }
    req.add_relation_type_filter(master::MATVIEW_TABLE_RELATION);
    return master_ddl_proxy_->ListTables(req, &resp, rpc);
  }));

  if (resp.tables_size() == 0) {
        return STATUS_FORMAT(InvalidArgument, "No tables found in namespace: $0", ns.name);
  }

  vector<YBTableName> tables(resp.tables_size());
  for (int i = 0; i < resp.tables_size(); ++i) {
        const auto& table = resp.tables(i);
        tables[i].set_table_id(table.id());
        tables[i].set_namespace_id(table.namespace_().id());
        tables[i].set_pgschema_name(table.pgschema_name());

        RSTATUS_DCHECK(
            table.relation_type() == master::USER_TABLE_RELATION ||
                table.relation_type() == master::INDEX_TABLE_RELATION ||
                table.relation_type() == master::MATVIEW_TABLE_RELATION,
            InternalError, Format("Invalid relation type: $0", table.relation_type()));
        RSTATUS_DCHECK_EQ(
            table.namespace_().name(), ns.name, InternalError,
            Format("Invalid namespace name: $0", table.namespace_().name()));
        RSTATUS_DCHECK_EQ(
            table.namespace_().database_type(), ns.db_type, InternalError,
            Format(
                "Invalid namespace type: $0",
                YQLDatabase_Name(table.namespace_().database_type())));
  }

  return CreateSnapshot(tables, retention_duration_hours, /* add_indexes */ false);
}

Result<ListSnapshotRestorationsResponsePB> ClusterAdminClient::ListSnapshotRestorations(
    const TxnSnapshotRestorationId& restoration_id) {
  master::ListSnapshotRestorationsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::ListSnapshotRestorationsRequestPB req;
    if (restoration_id) {
      req.set_restoration_id(restoration_id.data(), restoration_id.size());
    }
    return master_backup_proxy_->ListSnapshotRestorations(req, &resp, rpc);
  }));
  return resp;
}

Result<rapidjson::Document> ClusterAdminClient::CreateSnapshotSchedule(
    const client::YBTableName& keyspace, MonoDelta interval, MonoDelta retention) {
  master::CreateSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::CreateSnapshotScheduleRequestPB req;

    auto& options = *req.mutable_options();
    auto& filter_tables = *options.mutable_filter()->mutable_tables()->mutable_tables();
    keyspace.SetIntoTableIdentifierPB(filter_tables.Add());

    options.set_interval_sec(interval.ToSeconds());
    options.set_retention_duration_sec(retention.ToSeconds());
    return master_backup_proxy_->CreateSnapshotSchedule(req, &resp, rpc);
  }));

  rapidjson::Document document;
  document.SetObject();

  AddStringField(
      "schedule_id",
      VERIFY_RESULT(FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id())).ToString(),
      &document, &document.GetAllocator());
  return document;
}

Result<rapidjson::Document> ClusterAdminClient::ListSnapshotSchedules(
    const SnapshotScheduleId& schedule_id) {
  master::ListSnapshotSchedulesResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [this, &resp, &schedule_id](RpcController* rpc) {
    master::ListSnapshotSchedulesRequestPB req;
    if (schedule_id) {
      req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
    }
    return master_backup_proxy_->ListSnapshotSchedules(req, &resp, rpc);
  }));

  rapidjson::Document result;
  result.SetObject();
  rapidjson::Value json_schedules(rapidjson::kArrayType);
  for (const auto& schedule : resp.schedules()) {
        json_schedules.PushBack(
            VERIFY_RESULT(SnapshotScheduleInfoToJson(schedule, &result.GetAllocator())),
            result.GetAllocator());
  }
  result.AddMember("schedules", json_schedules, result.GetAllocator());
  return result;
}

Result<rapidjson::Document> ClusterAdminClient::DeleteSnapshotSchedule(
    const SnapshotScheduleId& schedule_id) {
  master::DeleteSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [this, &resp, &schedule_id](RpcController* rpc) {
    master::DeleteSnapshotScheduleRequestPB req;
    req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());

    return master_backup_proxy_->DeleteSnapshotSchedule(req, &resp, rpc);
  }));

  rapidjson::Document document;
  document.SetObject();
  AddStringField("schedule_id", schedule_id.ToString(), &document, &document.GetAllocator());
  return document;
}

bool SnapshotSuitableForRestoreAt(const SysSnapshotEntryPB& entry, HybridTime restore_at) {
  return (entry.state() == master::SysSnapshotEntryPB::COMPLETE ||
          entry.state() == master::SysSnapshotEntryPB::CREATING) &&
         HybridTime::FromPB(entry.snapshot_hybrid_time()) >= restore_at &&
         HybridTime::FromPB(entry.previous_snapshot_hybrid_time()) < restore_at;
}

Result<TxnSnapshotId> ClusterAdminClient::SuitableSnapshotId(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at, CoarseTimePoint deadline) {
  for (;;) {
        auto last_snapshot_time = HybridTime::kMin;
        {
      RpcController rpc;
      rpc.set_deadline(deadline);
      master::ListSnapshotSchedulesRequestPB req;
      master::ListSnapshotSchedulesResponsePB resp;
      if (schedule_id) {
        req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
      }

      RETURN_NOT_OK_PREPEND(
          master_backup_proxy_->ListSnapshotSchedules(req, &resp, &rpc),
          "Failed to list snapshot schedules");

      if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
      }

      if (resp.schedules().size() < 1) {
        return STATUS_FORMAT(InvalidArgument, "Unknown schedule: $0", schedule_id);
      }

      for (const auto& snapshot : resp.schedules()[0].snapshots()) {
        auto snapshot_hybrid_time = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
        last_snapshot_time = std::max(last_snapshot_time, snapshot_hybrid_time);
        if (SnapshotSuitableForRestoreAt(snapshot.entry(), restore_at)) {
          return VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
        }
      }
      if (last_snapshot_time > restore_at) {
        return STATUS_FORMAT(
            IllegalState, "Cannot restore at $0, last snapshot: $1, snapshots: $2", restore_at,
            last_snapshot_time, resp.schedules()[0].snapshots());
      }
        }
        RpcController rpc;
        rpc.set_deadline(deadline);
        master::CreateSnapshotRequestPB req;
        master::CreateSnapshotResponsePB resp;
        req.set_schedule_id(schedule_id.data(), schedule_id.size());
        RETURN_NOT_OK_PREPEND(
            master_backup_proxy_->CreateSnapshot(req, &resp, &rpc), "Failed to create snapshot");
        if (resp.has_error()) {
      auto status = StatusFromPB(resp.error().status());
      if (master::MasterError(status) == master::MasterErrorPB::PARALLEL_SNAPSHOT_OPERATION) {
        std::this_thread::sleep_until(std::min(deadline, CoarseMonoClock::now() + 1s));
        continue;
      }
      return status;
        }
        return FullyDecodeTxnSnapshotId(resp.snapshot_id());
  }
}

Status ClusterAdminClient::DisableTabletSplitsDuringRestore(CoarseTimePoint deadline) {
  // TODO(Sanket): Eventually all of this logic needs to be moved
  // to the master and exposed as APIs for the clients to consume.
  const auto splitting_disabled_until =
      CoarseMonoClock::Now() + MonoDelta::FromSeconds(kPitrSplitDisableDurationSecs);
  // Disable splitting and then wait for all pending splits to complete before
  // starting restoration.
  VERIFY_RESULT_PREPEND(
      DisableTabletSplitsInternal(kPitrSplitDisableDurationSecs * 1000, kPitrFeatureName),
      "Failed to disable tablet split before restore.");

  while (CoarseMonoClock::Now() < std::min(splitting_disabled_until, deadline)) {
        // Wait for existing split operations to complete.
        const auto resp = VERIFY_RESULT_PREPEND(
            IsTabletSplittingCompleteInternal(
                true /* wait_for_parent_deletion */,
                deadline - CoarseMonoClock::now() /* timeout */),
            "Tablet splitting did not complete. Cannot restore.");
        if (resp.is_tablet_splitting_complete()) {
      break;
        }
        SleepFor(MonoDelta::FromMilliseconds(kPitrSplitDisableCheckFreqMs));
  }

  if (CoarseMonoClock::now() >= deadline) {
        return STATUS(TimedOut, "Timed out waiting for tablet splitting to complete.");
  }

  // Return if we have used almost all of our time in waiting for splitting to complete,
  // since we can't guarantee that another split does not start.
  if (CoarseMonoClock::now() + MonoDelta::FromSeconds(3) >= splitting_disabled_until) {
        return STATUS(
            TimedOut, "Not enough time after disabling splitting to disable ", "splitting again.");
  }

  // Disable for kPitrSplitDisableDurationSecs again so the restore has the full amount of time with
  // splitting disables. This overwrites the previous value since the feature_name is the same so
  // overall the time is still kPitrSplitDisableDurationSecs.
  VERIFY_RESULT_PREPEND(
      DisableTabletSplitsInternal(kPitrSplitDisableDurationSecs * 1000, kPitrFeatureName),
      "Failed to disable tablet split before restore.");

  return Status::OK();
}

Result<rapidjson::Document> ClusterAdminClient::RestoreSnapshotScheduleDeprecated(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at) {
  auto deadline = CoarseMonoClock::now() + timeout_;

  // Disable splitting for the entire run of restore.
  RETURN_NOT_OK(DisableTabletSplitsDuringRestore(deadline));

  // Get the suitable snapshot to restore from.
  auto snapshot_id = VERIFY_RESULT(SuitableSnapshotId(schedule_id, restore_at, deadline));

  for (;;) {
        RpcController rpc;
        rpc.set_deadline(deadline);
        master::ListSnapshotsRequestPB req;
        req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
        master::ListSnapshotsResponsePB resp;
        RETURN_NOT_OK_PREPEND(
            master_backup_proxy_->ListSnapshots(req, &resp, &rpc), "Failed to list snapshots");
        if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
        }
        if (resp.snapshots().size() != 1) {
      return STATUS_FORMAT(
          IllegalState, "Wrong number of snapshots received $0", resp.snapshots().size());
        }
        if (resp.snapshots()[0].entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
      if (SnapshotSuitableForRestoreAt(resp.snapshots()[0].entry(), restore_at)) {
        break;
      }
      return STATUS_FORMAT(IllegalState, "Snapshot is not suitable for restore at $0", restore_at);
        }
        auto now = CoarseMonoClock::now();
        if (now >= deadline) {
      return STATUS_FORMAT(TimedOut, "Timed out to complete a snapshot $0", snapshot_id);
        }
        std::this_thread::sleep_until(std::min(deadline, now + 100ms));
  }

  RpcController rpc;
  rpc.set_deadline(deadline);
  RestoreSnapshotRequestPB req;
  RestoreSnapshotResponsePB resp;
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.set_restore_ht(restore_at.ToUint64());
  RETURN_NOT_OK_PREPEND(
      master_backup_proxy_->RestoreSnapshot(req, &resp, &rpc), "Failed to restore snapshot");

  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  auto restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()));

  rapidjson::Document document;
  document.SetObject();

  AddStringField("snapshot_id", snapshot_id.ToString(), &document, &document.GetAllocator());
  AddStringField("restoration_id", restoration_id.ToString(), &document, &document.GetAllocator());

  return document;
}

Result<rapidjson::Document> ClusterAdminClient::RestoreSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, HybridTime restore_at) {
  auto deadline = CoarseMonoClock::now() + timeout_;

  RpcController rpc;
  rpc.set_deadline(deadline);
  master::RestoreSnapshotScheduleRequestPB req;
  master::RestoreSnapshotScheduleResponsePB resp;
  req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
  req.set_restore_ht(restore_at.ToUint64());

  Status s = master_backup_proxy_->RestoreSnapshotSchedule(req, &resp, &rpc);
  if (!s.ok()) {
        if (s.IsRemoteError() &&
            rpc.error_response()->code() == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD) {
      cout << "WARNING: fallback to RestoreSnapshotScheduleDeprecated." << endl;
      return RestoreSnapshotScheduleDeprecated(schedule_id, restore_at);
        }
        RETURN_NOT_OK_PREPEND(
            s, Format("Failed to restore snapshot from schedule: $0", schedule_id.ToString()));
  }

  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(resp.snapshot_id()));
  auto restoration_id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()));

  rapidjson::Document document;
  document.SetObject();

  AddStringField("snapshot_id", snapshot_id.ToString(), &document, &document.GetAllocator());
  AddStringField("restoration_id", restoration_id.ToString(), &document, &document.GetAllocator());

  return document;
}

Result<rapidjson::Document> ClusterAdminClient::EditSnapshotSchedule(
    const SnapshotScheduleId& schedule_id, std::optional<MonoDelta> new_interval,
    std::optional<MonoDelta> new_retention) {
  master::EditSnapshotScheduleResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::EditSnapshotScheduleRequestPB req;
    req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
    if (new_interval) {
      req.set_interval_sec(new_interval->ToSeconds());
    }
    if (new_retention) {
      req.set_retention_duration_sec(new_retention->ToSeconds());
    }
    return master_backup_proxy_->EditSnapshotSchedule(req, &resp, rpc);
  }));
  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value json_schedule =
      VERIFY_RESULT(SnapshotScheduleInfoToJson(resp.schedule(), &document.GetAllocator()));
  document.AddMember("schedule", json_schedule, document.GetAllocator());
  return document;
}

Status ClusterAdminClient::RestoreSnapshot(const string& snapshot_id, HybridTime timestamp) {
  RestoreSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    RestoreSnapshotRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));
    if (timestamp) {
      req.set_restore_ht(timestamp.ToUint64());
    }
    return master_backup_proxy_->RestoreSnapshot(req, &resp, rpc);
  }));

  cout << "Started restoring snapshot: " << snapshot_id << endl
       << "Restoration id: " << FullyDecodeTxnSnapshotRestorationId(resp.restoration_id()) << endl;
  if (timestamp) {
        cout << "Restore at: " << timestamp << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::DeleteSnapshot(const std::string& snapshot_id) {
  DeleteSnapshotResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    DeleteSnapshotRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));
    return master_backup_proxy_->DeleteSnapshot(req, &resp, rpc);
  }));

  cout << "Deleted snapshot: " << snapshot_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::AbortSnapshotRestore(const TxnSnapshotRestorationId& restoration_id) {
  AbortSnapshotRestoreResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    AbortSnapshotRestoreRequestPB req;
    req.set_restoration_id(restoration_id.data(), restoration_id.size());
    return master_backup_proxy_->AbortSnapshotRestore(req, &resp, rpc);
  }));

  cout << "Aborted snapshot restore: " << restoration_id.ToString() << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateSnapshotMetaFile(
    const string& snapshot_id, const string& file_name) {
  ListSnapshotsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    ListSnapshotsRequestPB req;
    req.set_snapshot_id(StringToSnapshotId(snapshot_id));

    // Format 0 - latest format (== Format 2 at the moment).
    // Format -1 - old format (no 'namespace_name' in the Table entry).
    // Format 1 - old format.
    // Format 2 - new format.
    if (FLAGS_TEST_metadata_file_format_version == 0 ||
        FLAGS_TEST_metadata_file_format_version >= 2) {
      req.set_prepare_for_backup(true);
    }
    return master_backup_proxy_->ListSnapshots(req, &resp, rpc);
  }));

  if (resp.snapshots_size() > 1) {
        LOG(WARNING) << "Requested snapshot metadata for snapshot '" << snapshot_id << "', but got "
                     << resp.snapshots_size() << " snapshots in the response";
  }

  SnapshotInfoPB* snapshot = nullptr;
  for (SnapshotInfoPB& snapshot_entry : *resp.mutable_snapshots()) {
        if (SnapshotIdToString(snapshot_entry.id()) == snapshot_id) {
      snapshot = &snapshot_entry;
      break;
        }
  }
  if (!snapshot) {
        return STATUS_FORMAT(
            InternalError, "Response contained $0 entries but no entry for snapshot '$1'",
            resp.snapshots_size(), snapshot_id);
  }

  if (FLAGS_TEST_metadata_file_format_version == -1) {
        // Remove 'namespace_name' from SysTablesEntryPB.
        SysSnapshotEntryPB& sys_entry = *snapshot->mutable_entry();
        for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      if (entry.type() == SysRowEntryType::TABLE) {
        auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
        meta.clear_namespace_name();
        entry.set_data(meta.SerializeAsString());
      }
        }
  }

  cout << "Exporting snapshot " << snapshot_id << " (" << snapshot->entry().state() << ") to file "
       << file_name << endl;

  // Serialize snapshot protobuf to given path.
  RETURN_NOT_OK(pb_util::WritePBContainerToPath(
      Env::Default(), file_name, *snapshot, pb_util::OVERWRITE, pb_util::SYNC));

  cout << "Snapshot metadata was saved into file: " << file_name << endl;
  return Status::OK();
}

string ClusterAdminClient::GetDBTypeName(const SysNamespaceEntryPB& pb) {
  const YQLDatabase db_type = GetDatabaseType(pb);
  switch (db_type) {
        case YQL_DATABASE_UNKNOWN:
      return "UNKNOWN DB TYPE";
        case YQL_DATABASE_CQL:
      return "YCQL keyspace";
        case YQL_DATABASE_PGSQL:
      return "YSQL database";
        case YQL_DATABASE_REDIS:
      return "YEDIS namespace";
  }
  FATAL_INVALID_ENUM_VALUE(YQLDatabase, db_type);
}

Status ClusterAdminClient::UpdateUDTypes(
    QLTypePB* pb_type, bool* update_meta, const NSNameToNameMap& ns_name_to_name) {
  return IterateAndDoForUDT(
      pb_type, [update_meta, &ns_name_to_name](QLTypePB::UDTypeInfo* udtype_info) -> Status {
        auto ns_it = ns_name_to_name.find(udtype_info->keyspace_name());
        if (ns_it == ns_name_to_name.end()) {
          return STATUS_FORMAT(
              InvalidArgument, "No metadata for keyspace '$0' referenced in UDType '$1'",
              udtype_info->keyspace_name(), udtype_info->name());
        }

        if (udtype_info->keyspace_name() != ns_it->second) {
          udtype_info->set_keyspace_name(ns_it->second);
          *DCHECK_NOTNULL(update_meta) = true;
        }
        return Status::OK();
      });
}

class ImportSnapshotTableFilter {
 public:
  typedef std::unique_ptr<ImportSnapshotTableFilter> Ptr;

  explicit ImportSnapshotTableFilter(SnapshotInfoPB* req_snapshot_info) :
    req_snapshot_info_(req_snapshot_info), table_index_(0) {}
  virtual ~ImportSnapshotTableFilter() {}

  virtual YBTableName CurrentInputTable() const { return YBTableName(); }
  virtual bool IsDatabaseTypeSupported(YQLDatabase type) const { return true; }
  virtual bool ShouldProcessTable(const string &table_name, const string &table_id) {
    // Default is to process all tables
    table_index_++;
    return true;
  }
  virtual Status UpdateRequestSnapshotInfo() { return Status::OK(); }
  virtual SnapshotInfoPB* WorkingSnapshotInfo() { return req_snapshot_info_; }
  virtual size_t CurrentTableIndex() const { return table_index_; }
 protected:
  SnapshotInfoPB* req_snapshot_info_;
  size_t table_index_;
};

class ImportSnapshotRenameAllTables : public ImportSnapshotTableFilter {
 public:
  ImportSnapshotRenameAllTables(
      SnapshotInfoPB* req_snapshot_info, const vector<YBTableName> tables) :
    ImportSnapshotTableFilter(req_snapshot_info), tables_(tables) {}

  virtual YBTableName CurrentInputTable() const {
    return table_index_ < tables_.size() ? tables_[table_index_] : YBTableName();
  }
 protected:
  std::vector<YBTableName> tables_;
};

class ImportSnapshotSelectiveTableFilter : public ImportSnapshotTableFilter {
 public:
  ImportSnapshotSelectiveTableFilter(
      SnapshotInfoPB* req_snapshot_info, const vector<YBTableName> tables) :
    ImportSnapshotTableFilter(req_snapshot_info) {
    for (auto &t : tables) {
      table_name_map_[t.table_name()] = t;
    }
  }
  virtual bool IsDatabaseTypeSupported(YQLDatabase type) const {
    return type == YQL_DATABASE_CQL ? true : false;
  }

  virtual bool ShouldProcessTable(const string &table_name, const string &table_id) {
    auto table_it = table_name_map_.find(table_name);
    if (table_it == table_name_map_.end()) {
      return false;
    }
    table_it->second.set_table_id(table_id);
    table_id_map_[table_id] = table_name;
    table_index_++;
    return true;
  }
  virtual SnapshotInfoPB* WorkingSnapshotInfo() { return &snapshot_info_; }
  virtual Status UpdateRequestSnapshotInfo();
 private:
  std::unordered_map<string, YBTableName> table_name_map_;
  std::unordered_map<string, string> table_id_map_;
  master::SnapshotInfoPB snapshot_info_;
};

Status ImportSnapshotSelectiveTableFilter::UpdateRequestSnapshotInfo() {
  // Do sanity checks
  for (auto& it : table_name_map_) {
    if (!it.second.has_table_id()) {
      return STATUS_FORMAT(
          InvalidArgument, "Table $0 not found in backup", it.second.table_name());
    }
    auto id_it = table_id_map_.find(it.second.table_id());
    if (id_it == table_id_map_.end()) {
      return STATUS_FORMAT(
          InvalidArgument, "Table id $0 not found in map", it.second.table_id());
    }
    CHECK_EQ(id_it->second, it.second.table_name());
  }

  for (BackupRowEntryPB& backup_entry : *snapshot_info_.mutable_backup_entries()) {
    SysRowEntry& entry = *backup_entry.mutable_entry();
    if (entry.type() == SysRowEntryType::TABLET) {
      auto meta = VERIFY_RESULT(ParseFromSlice<SysTabletsEntryPB>(entry.data()));
      auto id_it = table_id_map_.find(meta.table_id());
      if (id_it == table_id_map_.end()) {
        entry.set_data("");
      }
    }
    if (entry.data().length()) {
      req_snapshot_info_->add_backup_entries()->CopyFrom(backup_entry);
    }
  }
  if (snapshot_info_.has_id()) {
    req_snapshot_info_->set_id(snapshot_info_.id());
  }
  if (snapshot_info_.has_entry()) {
    req_snapshot_info_->mutable_entry()->CopyFrom(snapshot_info_.entry());
  }
  if (snapshot_info_.has_format_version()) {
    req_snapshot_info_->set_format_version(snapshot_info_.format_version());
  }
  return Status::OK();
}

Status ClusterAdminClient::ProcessSnapshotInfoPBFile(
    const string& file_name,
    const TypedNamespaceName& keyspace,
    ImportSnapshotTableFilter *table_filter) {
  SnapshotInfoPB* snapshot_info = table_filter->WorkingSnapshotInfo();
  // Read snapshot protobuf from given path.
  RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(Env::Default(), file_name, snapshot_info));

  if (!snapshot_info->has_format_version()) {
    SCHECK_EQ(
        0, snapshot_info->backup_entries_size(), InvalidArgument,
        Format( "Metadata file in Format 1 has backup entries from Format 2: $0",
          snapshot_info->backup_entries_size()));

    // Repack PB data loaded in the old format.
    // Init BackupSnapshotPB based on loaded SnapshotInfoPB.
    SysSnapshotEntryPB& sys_entry = *snapshot_info->mutable_entry();
    snapshot_info->mutable_backup_entries()->Reserve(sys_entry.entries_size());
    for (SysRowEntry& entry : *sys_entry.mutable_entries()) {
      snapshot_info->add_backup_entries()->mutable_entry()->Swap(&entry);
    }

    sys_entry.clear_entries();
    snapshot_info->set_format_version(2);
  }

  cout << "Importing snapshot " << SnapshotIdToString(snapshot_info->id()) << " ("
       << snapshot_info->entry().state() << ")" << endl;

  // Map: Old namespace ID -> [Old name, New name] pair.
  typedef pair<NamespaceName, NamespaceName> NSNamePair;
  typedef std::unordered_map<NamespaceId, NSNamePair> NSIdToNameMap;
  NSIdToNameMap ns_id_to_name;
  NSNameToNameMap ns_name_to_name;

  bool was_table_renamed = false;
  for (BackupRowEntryPB& backup_entry : *snapshot_info->mutable_backup_entries()) {
    SysRowEntry& entry = *backup_entry.mutable_entry();
    YBTableName table_name = table_filter->CurrentInputTable();

    switch (entry.type()) {
      case SysRowEntryType::NAMESPACE: {
        auto meta = VERIFY_RESULT(ParseFromSlice<SysNamespaceEntryPB>(entry.data()));
        if (!table_filter->IsDatabaseTypeSupported(meta.database_type())) {
          return STATUS_FORMAT(
              InvalidArgument, "Selective restore is supported only for YCQL keyspaces");
        }
        const string db_type = GetDBTypeName(meta);
        cout << "Target imported " << db_type
             << " name: " << (keyspace.name.empty() ? meta.name() : keyspace.name) << endl
             << db_type << " being imported: " << meta.name() << endl;

        if (!keyspace.name.empty() && keyspace.name != meta.name()) {
          ns_id_to_name[entry.id()] = NSNamePair(meta.name(), keyspace.name);
          ns_name_to_name[meta.name()] = keyspace.name;

          meta.set_name(keyspace.name);
          entry.set_data(meta.SerializeAsString());
        } else {
          ns_id_to_name[entry.id()] = NSNamePair(meta.name(), meta.name());
          ns_name_to_name[meta.name()] = meta.name();
        }
        break;
      }
      case SysRowEntryType::UDTYPE: {
        // Note: UDT renaming is not supported.
        auto meta = VERIFY_RESULT(ParseFromSlice<SysUDTypeEntryPB>(entry.data()));
        const auto ns_it = ns_id_to_name.find(meta.namespace_id());
        cout << "Target imported user-defined type name: "
             << (ns_it == ns_id_to_name.end() ? "[" + meta.namespace_id() + "]"
                                              : ns_it->second.second)
             << "." << meta.name() << endl
             << "User-defined type being imported: "
             << (ns_it == ns_id_to_name.end() ? "[" + meta.namespace_id() + "]"
                                              : ns_it->second.first)
             << "." << meta.name() << endl;

        bool update_meta = false;
        // Update QLTypePB::UDTypeInfo::keyspace_name in the UDT params.
        for (int i = 0; i < meta.field_names_size(); ++i) {
          RETURN_NOT_OK(UpdateUDTypes(meta.mutable_field_types(i), &update_meta, ns_name_to_name));
        }

        if (update_meta) {
          entry.set_data(meta.SerializeAsString());
        }
        break;
      }
      case SysRowEntryType::TABLE: {
        if (was_table_renamed && table_name.empty()) {
          // Renaming is allowed for all tables OR for no one table.
          return STATUS_FORMAT(
              InvalidArgument,
              "There is no name for table (including indexes) number: $0",
              table_filter->CurrentTableIndex());
        }

        auto meta = VERIFY_RESULT(ParseFromSlice<SysTablesEntryPB>(entry.data()));
        if (!table_filter->ShouldProcessTable(meta.name(), entry.id())) {
          entry.set_data("");
          break;
        }
        const auto colocated_prefix = meta.colocated() ? "colocated " : "";

        if (meta.indexed_table_id().empty()) {
          cout << "Table type: " << colocated_prefix << "table" << endl;
        } else {
          cout << "Table type: " << colocated_prefix << "index (attaching to the old table id "
               << meta.indexed_table_id() << ")" << endl;
        }

        if (!table_name.empty()) {
          DCHECK(table_name.has_namespace());
          DCHECK(table_name.has_table());
          cout << "Target imported " << colocated_prefix << "table name: " << table_name.ToString()
               << endl;
        } else if (!keyspace.name.empty()) {
          if (IsColocatedDbParentTableName(meta.name())) {
            // Check whether the import_snapshot command is invoked for a colocation migration.
            // And adjust the output message if a colocation migration happens.
            master::GetNamespaceInfoResponsePB ns_resp;
            RETURN_NOT_OK(RequestMasterLeader(&ns_resp, [&](RpcController* rpc) {
              master::GetNamespaceInfoRequestPB req;
              req.mutable_namespace_()->set_name(keyspace.name);
              req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
              return master_ddl_proxy_->GetNamespaceInfo(req, &ns_resp, rpc);
            }));
            if (ns_resp.has_legacy_colocated_database()
                && !ns_resp.legacy_colocated_database()) {
              master::ListTablegroupsResponsePB resp;
              RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
                master::ListTablegroupsRequestPB req;
                req.set_namespace_id(ns_resp.namespace_().id());
                return master_ddl_proxy_->ListTablegroups(req, &resp, rpc);
              }));
              // For colocation migration, there should be only one default tablegroup in the target
              // imported database because we don't support tablespace in a legacy colocated
              // database.
              DCHECK_EQ(resp.tablegroups().size(), 1);
              cout << "Target imported " << colocated_prefix << "table name: "
                   << keyspace.name << "."
                   << GetColocationParentTableName(resp.tablegroups()[0].id()) << endl;
            }
          } else {
            cout << "Target imported " << colocated_prefix << "table name: " << keyspace.name << "."
                 << meta.name() << endl;
          }
        }

        // Print old table name before the table renaming in the code below.
        cout << (meta.colocated() ? "Colocated t" : "T") << "able being imported: "
             << (meta.namespace_name().empty() ? "[" + meta.namespace_id() + "]"
                                               : meta.namespace_name())
             << "." << meta.name() << endl;

        bool update_meta = false;
        if (!table_name.empty() && table_name.table_name() != meta.name()) {
          meta.set_name(table_name.table_name());
          update_meta = true;
          was_table_renamed = true;
        }
        if (!keyspace.name.empty() && keyspace.name != meta.namespace_name()) {
          meta.set_namespace_name(keyspace.name);
          update_meta = true;
        }

        if (meta.name().empty()) {
          return STATUS(IllegalState, "Could not find table name from snapshot metadata");
        }

        // Update QLTypePB::UDTypeInfo::keyspace_name in all UDT params in the table Schema.
        SchemaPB* const schema = meta.mutable_schema();
        // Recursively update ids in used user-defined types.
        for (int i = 0; i < schema->columns_size(); ++i) {
          QLTypePB* const pb_type = schema->mutable_columns(i)->mutable_type();
          RETURN_NOT_OK(UpdateUDTypes(pb_type, &update_meta, ns_name_to_name));
        }

        // Update the table name if needed.
        if (update_meta) {
          entry.set_data(meta.SerializeAsString());
        }

        break;
      }
      default:
        break;
    }
  }

  RETURN_NOT_OK(table_filter->UpdateRequestSnapshotInfo());
  return Status::OK();
}

Status ClusterAdminClient::ImportSnapshotMetaFile(
    const string& file_name,
    const TypedNamespaceName& keyspace,
    const vector<YBTableName>& tables,
    bool selective_import) {
  ImportSnapshotMetaRequestPB req;
  SnapshotInfoPB* snapshot_info = req.mutable_snapshot();

  ImportSnapshotTableFilter::Ptr table_filter_ptr;
  if (!tables.size()) {
    table_filter_ptr = std::make_unique<ImportSnapshotTableFilter>(snapshot_info);
  } else if (!selective_import) {
    table_filter_ptr = std::make_unique<ImportSnapshotRenameAllTables>(snapshot_info, tables);
  } else {
    table_filter_ptr = std::make_unique<ImportSnapshotSelectiveTableFilter>(snapshot_info, tables);
  }
  cout << "Read snapshot meta file " << file_name << endl;

  RETURN_NOT_OK(ProcessSnapshotInfoPBFile(file_name, keyspace, table_filter_ptr.get()));
  size_t table_index = table_filter_ptr->CurrentTableIndex();

  // RPC timeout is a function of the number of tables that needs to be imported.
  ImportSnapshotMetaResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(
      &resp,
      [&](RpcController* rpc) { return master_backup_proxy_->ImportSnapshotMeta(req, &resp, rpc); },
      timeout_ * std::max(static_cast<size_t>(1), table_index)));

  const int kObjectColumnWidth = 16;
  const auto pad_object_type = [](const string& s) {
    return RightPadToWidth(s, kObjectColumnWidth);
  };

  cout << "Successfully applied snapshot." << endl
       << pad_object_type("Object") << kColumnSep << RightPadToUuidWidth("Old ID") << kColumnSep
       << RightPadToUuidWidth("New ID") << endl;

  const RepeatedPtrField<ImportSnapshotMetaResponsePB_TableMetaPB>& tables_meta =
      resp.tables_meta();
  CreateSnapshotRequestPB snapshot_req;
  CreateSnapshotResponsePB snapshot_resp;

  for (int i = 0; i < tables_meta.size(); ++i) {
        const ImportSnapshotMetaResponsePB_TableMetaPB& table_meta = tables_meta.Get(i);
        const string& new_table_id = table_meta.table_ids().new_id();

        cout << pad_object_type("Keyspace") << kColumnSep << table_meta.namespace_ids().old_id()
             << kColumnSep << table_meta.namespace_ids().new_id() << endl;

        if (!ImportSnapshotMetaResponsePB_TableType_IsValid(table_meta.table_type())) {
          return STATUS_FORMAT(InternalError, "Found unknown table type: ",
              table_meta.table_type());
        }

        const string table_type = AllCapsToCamelCase(
            ImportSnapshotMetaResponsePB_TableType_Name(table_meta.table_type()));
        cout << pad_object_type(table_type) << kColumnSep << table_meta.table_ids().old_id()
             << kColumnSep << new_table_id << endl;

        const RepeatedPtrField<IdPairPB>& udts_map = table_meta.ud_types_ids();
        for (int j = 0; j < udts_map.size(); ++j) {
          const IdPairPB& pair = udts_map.Get(j);
          cout << pad_object_type("UDType") << kColumnSep << pair.old_id() << kColumnSep
            << pair.new_id() << endl;
        }

        const RepeatedPtrField<IdPairPB>& tablets_map = table_meta.tablets_ids();
        for (int j = 0; j < tablets_map.size(); ++j) {
          const IdPairPB& pair = tablets_map.Get(j);
          cout << pad_object_type(Format("Tablet $0", j)) << kColumnSep << pair.old_id()
            << kColumnSep << pair.new_id() << endl;
        }

        RETURN_NOT_OK(yb_client_->WaitForCreateTableToFinish(
            new_table_id,
            CoarseMonoClock::Now() +
                MonoDelta::FromSeconds(FLAGS_yb_client_admin_operation_timeout_sec)));

        snapshot_req.mutable_tables()->Add()->set_table_id(new_table_id);
  }

  // All indexes already are in the request. Do not add them twice.
  snapshot_req.set_add_indexes(false);
  snapshot_req.set_transaction_aware(true);
  snapshot_req.set_imported(true);
  // Create new snapshot.
  RETURN_NOT_OK(RequestMasterLeader(&snapshot_resp, [&](RpcController* rpc) {
    return master_backup_proxy_->CreateSnapshot(snapshot_req, &snapshot_resp, rpc);
  }));

  cout << pad_object_type("Snapshot") << kColumnSep << SnapshotIdToString(snapshot_info->id())
       << kColumnSep << SnapshotIdToString(snapshot_resp.snapshot_id()) << endl;

  return Status::OK();
}

Status ClusterAdminClient::ListReplicaTypeCounts(const YBTableName& table_name) {
  vector<string> tablet_ids, ranges;
  RETURN_NOT_OK(yb_client_->GetTablets(table_name, 0, &tablet_ids, &ranges));
  master::GetTabletLocationsResponsePB resp;
  RETURN_NOT_OK(RequestMasterLeader(&resp, [&](RpcController* rpc) {
    master::GetTabletLocationsRequestPB req;
    for (const auto& tablet_id : tablet_ids) {
      req.add_tablet_ids(tablet_id);
    }
    return master_client_proxy_->GetTabletLocations(req, &resp, rpc);
  }));

  struct ReplicaCounts {
        int live_count;
        int read_only_count;
        string placement_uuid;
  };
  std::map<TabletServerId, ReplicaCounts> replica_map;

  std::cout << "Tserver ID\t\tPlacement ID\t\tLive count\t\tRead only count\n";

  for (int tablet_idx = 0; tablet_idx < resp.tablet_locations_size(); tablet_idx++) {
        const master::TabletLocationsPB& locs = resp.tablet_locations(tablet_idx);
        for (int replica_idx = 0; replica_idx < locs.replicas_size(); replica_idx++) {
      const auto& replica = locs.replicas(replica_idx);
      const string& ts_uuid = replica.ts_info().permanent_uuid();
      const string& placement_uuid =
          replica.ts_info().has_placement_uuid() ? replica.ts_info().placement_uuid() : "";
      bool is_replica_read_only =
          replica.member_type() == consensus::PeerMemberType::PRE_OBSERVER ||
          replica.member_type() == consensus::PeerMemberType::OBSERVER;
      int live_count = is_replica_read_only ? 0 : 1;
      int read_only_count = 1 - live_count;
      if (replica_map.count(ts_uuid) == 0) {
        replica_map[ts_uuid].live_count = live_count;
        replica_map[ts_uuid].read_only_count = read_only_count;
        replica_map[ts_uuid].placement_uuid = placement_uuid;
      } else {
        ReplicaCounts* counts = &replica_map[ts_uuid];
        counts->live_count += live_count;
        counts->read_only_count += read_only_count;
      }
        }
  }

  for (auto const& tserver : replica_map) {
        std::cout << tserver.first << "\t\t" << tserver.second.placement_uuid << "\t\t"
                  << tserver.second.live_count << "\t\t" << tserver.second.read_only_count
                  << std::endl;
  }

  return Status::OK();
}

Status ClusterAdminClient::SetPreferredZones(const std::vector<string>& preferred_zones) {
  rpc::RpcController rpc;
  master::SetPreferredZonesRequestPB req;
  master::SetPreferredZonesResponsePB resp;
  rpc.set_timeout(timeout_);

  std::set<string> zones;
  std::set<int> visited_priorities;

  for (const string& zone : preferred_zones) {
        if (zones.find(zone) != zones.end()) {
      return STATUS_SUBSTITUTE(
          InvalidArgument, "Invalid argument for preferred zone $0, values should not repeat",
          zone);
        }

        std::vector<std::string> zone_priority_split =
            strings::Split(zone, ":", strings::AllowEmpty());
        if (zone_priority_split.size() == 0 || zone_priority_split.size() > 2) {
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Invalid argument for preferred zone $0, should have format cloud.region.zone[:priority]",
          zone);
        }

        std::vector<string> tokens =
            strings::Split(zone_priority_split[0], ".", strings::AllowEmpty());
        if (tokens.size() != 3) {
      return STATUS_SUBSTITUTE(
          InvalidArgument,
          "Invalid argument for preferred zone $0, should have format cloud.region.zone:[priority]",
          zone);
        }

        uint priority = 1;

        if (zone_priority_split.size() == 2) {
      auto result = CheckedStoi(zone_priority_split[1]);
      if (!result.ok() || result.get() < 1) {
        return STATUS_SUBSTITUTE(
            InvalidArgument,
            "Invalid argument for preferred zone $0, priority should be non-zero positive integer",
            zone);
      }
      priority = static_cast<uint>(result.get());
        }

        // Max priority if each zone has a unique priority value can only be the size of the input
        // array
        if (priority > preferred_zones.size()) {
      return STATUS(
          InvalidArgument,
          "Priority value cannot be more than the number of zones in the preferred list since each "
          "priority should be associated with at least one zone from the list");
        }

        CloudInfoPB* cloud_info = nullptr;
        visited_priorities.insert(priority);

        while (req.multi_preferred_zones_size() < static_cast<int>(priority)) {
      req.add_multi_preferred_zones();
        }

        auto current_list = req.mutable_multi_preferred_zones(priority - 1);
        cloud_info = current_list->add_zones();

        cloud_info->set_placement_cloud(tokens[0]);
        cloud_info->set_placement_region(tokens[1]);
        cloud_info->set_placement_zone(tokens[2]);

        zones.emplace(zone);

        if (priority == 1) {
      // Handle old clusters which can only handle a single priority. New clusters will ignore this
      // member as multi_preferred_zones is already set.
      cloud_info = req.add_preferred_zones();
      cloud_info->set_placement_cloud(tokens[0]);
      cloud_info->set_placement_region(tokens[1]);
      cloud_info->set_placement_zone(tokens[2]);
        }
  }

  int size = static_cast<int>(visited_priorities.size());
  if (size > 0 && (*(visited_priorities.rbegin()) != size)) {
        return STATUS_SUBSTITUTE(
            InvalidArgument, "Invalid argument, each priority should have at least one zone");
  }

  RETURN_NOT_OK(master_cluster_proxy_->SetPreferredZones(req, &resp, &rpc));

  if (resp.has_error()) {
        return STATUS(ServiceUnavailable, resp.error().status().message());
  }

  return Status::OK();
}

Status ClusterAdminClient::RotateUniverseKey(const std::string& key_path) {
  return SendEncryptionRequest(key_path, true);
}

Status ClusterAdminClient::DisableEncryption() { return SendEncryptionRequest("", false); }

Status ClusterAdminClient::SendEncryptionRequest(
    const std::string& key_path, bool enable_encryption) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  // Get the cluster config from the master leader.
  master::ChangeEncryptionInfoRequestPB encryption_info_req;
  master::ChangeEncryptionInfoResponsePB encryption_info_resp;
  encryption_info_req.set_encryption_enabled(enable_encryption);
  if (key_path != "") {
        encryption_info_req.set_key_path(key_path);
  }
  RETURN_NOT_OK_PREPEND(
      master_encryption_proxy_->ChangeEncryptionInfo(
          encryption_info_req, &encryption_info_resp, &rpc),
      "MasterServiceImpl::ChangeEncryptionInfo call fails.")

  if (encryption_info_resp.has_error()) {
        return StatusFromPB(encryption_info_resp.error().status());
  }
  return Status::OK();
}

Status ClusterAdminClient::IsEncryptionEnabled() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::IsEncryptionEnabledRequestPB req;
  master::IsEncryptionEnabledResponsePB resp;
  RETURN_NOT_OK_PREPEND(
      master_encryption_proxy_->IsEncryptionEnabled(req, &resp, &rpc),
      "MasterServiceImpl::IsEncryptionEnabled call fails.");
  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  std::cout << "Encryption status: "
            << (resp.encryption_enabled() ? Format("ENABLED with key id $0", resp.key_id())
                                          : "DISABLED")
            << std::endl;
  return Status::OK();
}

Status ClusterAdminClient::AddUniverseKeyToAllMasters(
    const std::string& key_id, const std::string& universe_key) {
  RETURN_NOT_OK(encryption::EncryptionParams::IsValidKeySize(
      universe_key.size() - encryption::EncryptionParams::kBlockSize));

  master::AddUniverseKeysRequestPB req;
  master::AddUniverseKeysResponsePB resp;
  auto* universe_keys = req.mutable_universe_keys();
  (*universe_keys->mutable_map())[key_id] = universe_key;

  for (auto hp : VERIFY_RESULT(HostPort::ParseStrings(master_addr_list_, 7100))) {
        rpc::RpcController rpc;
        rpc.set_timeout(timeout_);
        master::MasterEncryptionProxy proxy(proxy_cache_.get(), hp);
        RETURN_NOT_OK_PREPEND(
            proxy.AddUniverseKeys(req, &resp, &rpc),
            Format("MasterServiceImpl::AddUniverseKeys call fails on host $0.", hp.ToString()));
        if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
        }
        std::cout << Format("Successfully added key to the node $0.\n", hp.ToString());
  }

  return Status::OK();
}

Status ClusterAdminClient::AllMastersHaveUniverseKeyInMemory(const std::string& key_id) {
  master::HasUniverseKeyInMemoryRequestPB req;
  master::HasUniverseKeyInMemoryResponsePB resp;
  req.set_version_id(key_id);

  for (auto hp : VERIFY_RESULT(HostPort::ParseStrings(master_addr_list_, 7100))) {
        rpc::RpcController rpc;
        rpc.set_timeout(timeout_);
        master::MasterEncryptionProxy proxy(proxy_cache_.get(), hp);
        RETURN_NOT_OK_PREPEND(
            proxy.HasUniverseKeyInMemory(req, &resp, &rpc),
            "MasterServiceImpl::ChangeEncryptionInfo call fails.");

        if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
        }
        if (!resp.has_key()) {
      return STATUS_FORMAT(TryAgain, "Node $0 does not have universe key in memory", hp);
        }

        std::cout << Format(
            "Node $0 has universe key in memory: $1\n", hp.ToString(), resp.has_key());
  }

  return Status::OK();
}

Status ClusterAdminClient::RotateUniverseKeyInMemory(const std::string& key_id) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::ChangeEncryptionInfoRequestPB req;
  master::ChangeEncryptionInfoResponsePB resp;
  req.set_encryption_enabled(true);
  req.set_in_memory(true);
  req.set_version_id(key_id);
  RETURN_NOT_OK_PREPEND(
      master_encryption_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
      "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  std::cout << "Rotated universe key in memory\n";
  return Status::OK();
}

Status ClusterAdminClient::DisableEncryptionInMemory() {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::ChangeEncryptionInfoRequestPB req;
  master::ChangeEncryptionInfoResponsePB resp;
  req.set_encryption_enabled(false);
  RETURN_NOT_OK_PREPEND(
      master_encryption_proxy_->ChangeEncryptionInfo(req, &resp, &rpc),
      "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  std::cout << "Encryption disabled\n";
  return Status::OK();
}

Status ClusterAdminClient::WriteUniverseKeyToFile(
    const std::string& key_id, const std::string& file_name) {
  RETURN_NOT_OK_PREPEND(WaitUntilMasterLeaderReady(), "Wait for master leader failed!");
  rpc::RpcController rpc;
  rpc.set_timeout(timeout_);

  master::GetUniverseKeyRegistryRequestPB req;
  master::GetUniverseKeyRegistryResponsePB resp;
  RETURN_NOT_OK_PREPEND(
      master_encryption_proxy_->GetUniverseKeyRegistry(req, &resp, &rpc),
      "MasterServiceImpl::ChangeEncryptionInfo call fails.");
  if (resp.has_error()) {
        return StatusFromPB(resp.error().status());
  }

  auto universe_keys = resp.universe_keys();
  const auto& it = universe_keys.map().find(key_id);
  if (it == universe_keys.map().end()) {
        return STATUS_FORMAT(NotFound, "Could not find key with id $0", key_id);
  }

  RETURN_NOT_OK(WriteStringToFile(Env::Default(), Slice(it->second), file_name));

  std::cout << "Finished writing to file\n";
  return Status::OK();
}

Status ClusterAdminClient::CreateCDCSDKDBStream(
    const TypedNamespaceName& ns, const std::string& checkpoint_type,
    const cdc::CDCRecordType record_type,
    const std::string& consistent_snapshot_option) {
  HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS());
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(proxy_cache_.get(), ts_addr);

  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;

  req.set_namespace_name(ns.name);
  req.set_db_type(ns.db_type);
  req.set_record_type(record_type);

  req.set_record_format(cdc::CDCRecordFormat::PROTO);
  req.set_source_type(cdc::CDCRequestSource::CDCSDK);
  if (checkpoint_type == yb::ToString("EXPLICIT")) {
        req.set_checkpoint_type(cdc::CDCCheckpointType::EXPLICIT);
  } else {
        req.set_checkpoint_type(cdc::CDCCheckpointType::IMPLICIT);
  }

  if (consistent_snapshot_option == "USE_SNAPSHOT") {
    req.set_cdcsdk_consistent_snapshot_option(CDCSDKSnapshotOption::USE_SNAPSHOT);
  } else {
    req.set_cdcsdk_consistent_snapshot_option(CDCSDKSnapshotOption::NOEXPORT_SNAPSHOT);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error creating stream: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "CDC Stream ID: " << resp.db_stream_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::CreateCDCStream(const TableId& table_id) {
  master::CreateCDCStreamRequestPB req;
  master::CreateCDCStreamResponsePB resp;
  req.set_table_id(table_id);
  req.mutable_options()->Reserve(3);

  auto record_type_option = req.add_options();
  record_type_option->set_key(cdc::kRecordType);
  record_type_option->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  auto record_format_option = req.add_options();
  record_format_option->set_key(cdc::kRecordFormat);
  record_format_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::JSON));

  auto source_type_option = req.add_options();
  source_type_option->set_key(cdc::kSourceType);
  source_type_option->set_value(CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->CreateCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error creating stream: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "CDC Stream ID: " << resp.stream_id() << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteCDCSDKDBStream(const std::string& db_stream_id) {
  master::DeleteCDCStreamRequestPB req;
  master::DeleteCDCStreamResponsePB resp;
  req.add_stream_id(db_stream_id);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->DeleteCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error deleting stream: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Successfully deleted Change Data Stream ID: " << db_stream_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteCDCStream(const std::string& stream_id, bool force_delete) {
  master::DeleteCDCStreamRequestPB req;
  master::DeleteCDCStreamResponsePB resp;
  req.add_stream_id(stream_id);
  req.set_force_delete(force_delete);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->DeleteCDCStream(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error deleting stream: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Successfully deleted CDC Stream ID: " << stream_id << endl;
  return Status::OK();
}

Status ClusterAdminClient::ListCDCStreams(const TableId& table_id) {
  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;
  req.set_id_type(yb::master::IdTypePB::TABLE_ID);
  if (!table_id.empty()) {
        req.set_table_id(table_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error getting CDC stream list: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "CDC Streams: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::ListCDCSDKStreams(const std::string& namespace_name) {
  master::ListCDCStreamsRequestPB req;
  master::ListCDCStreamsResponsePB resp;
  req.set_id_type(yb::master::IdTypePB::NAMESPACE_ID);

  if (!namespace_name.empty()) {
        cout << "Filtering out DB streams for the namespace: " << namespace_name << "\n\n";
        master::GetNamespaceInfoResponsePB namespace_info_resp;
        RETURN_NOT_OK(yb_client_->GetNamespaceInfo(
            "", namespace_name, YQL_DATABASE_PGSQL, &namespace_info_resp));
        req.set_namespace_id(namespace_info_resp.namespace_().id());
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->ListCDCStreams(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error getting CDC stream list: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "CDC Streams: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::GetCDCDBStreamInfo(const std::string& db_stream_id) {
  master::GetCDCDBStreamInfoRequestPB req;
  master::GetCDCDBStreamInfoResponsePB resp;
  req.set_db_stream_id(db_stream_id);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetCDCDBStreamInfo(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error getting info corresponding to CDC db stream : "
             << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "CDC DB Stream Info: \r\n" << resp.DebugString();
  return Status::OK();
}

Status ClusterAdminClient::YsqlBackfillReplicationSlotNameToCDCSDKStream(
    const std::string& stream_id, const std::string& replication_slot_name) {
  master::YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  master::YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_stream_id(stream_id);
  req.set_cdcsdk_ysql_replication_slot_name(replication_slot_name);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(
      master_replication_proxy_->YsqlBackfillReplicationSlotNameToCDCSDKStream(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error CDC stream with replication slot: " << resp.error().status().message()
             << endl;
        return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status ClusterAdminClient::WaitForSetupUniverseReplicationToFinish(
    const string& replication_group_id) {
  master::IsSetupUniverseReplicationDoneRequestPB req;
  req.set_replication_group_id(replication_group_id);
  for (;;) {
        master::IsSetupUniverseReplicationDoneResponsePB resp;
        RpcController rpc;
        rpc.set_timeout(timeout_);
        Status s = master_replication_proxy_->IsSetupUniverseReplicationDone(req, &resp, &rpc);

        if (!s.ok() || resp.has_error()) {
      LOG(WARNING) << "Encountered error while waiting for setup_universe_replication to complete"
                   << " : " << (!s.ok() ? s.ToString() : resp.error().status().message());
        }
        if (resp.has_done() && resp.done()) {
      return StatusFromPB(resp.replication_error());
        }

        // Still processing, wait and then loop again.
        std::this_thread::sleep_for(100ms);
  }
}

using ReplicationBootstrapState = master::SysUniverseReplicationBootstrapEntryPB::State;
Status ClusterAdminClient::WaitForReplicationBootstrapToFinish(const std::string& replication_id) {
  const auto initial_delay = MonoDelta::FromMilliseconds(100);
  const auto delay_increment = MonoDelta::FromMilliseconds(250);
  const auto max_delay_time = MonoDelta::FromSeconds(5);
  auto delay_time = initial_delay;

  master::IsSetupNamespaceReplicationWithBootstrapDoneRequestPB req;
  ReplicationBootstrapState state =
      ReplicationBootstrapState::SysUniverseReplicationBootstrapEntryPB_State_INITIALIZING;
  req.set_replication_group_id(replication_id);
  for (;;) {
        master::IsSetupNamespaceReplicationWithBootstrapDoneResponsePB resp;
        RpcController rpc;
        rpc.set_timeout(timeout_);
        Status s = master_replication_proxy_->IsSetupNamespaceReplicationWithBootstrapDone(
            req, &resp, &rpc);

        if (!s.ok() || resp.has_error()) {
      LOG(WARNING) << Format(
          "Encountered error while waiting for setup_namespace_replication_with_bootstrap to "
          "complete : $0",
          !s.ok() ? s.ToString() : resp.error().status().message());
        }
        if (resp.has_done() && resp.done()) {
      return StatusFromPB(resp.bootstrap_error());
        }
        if (resp.state() != state) {
      state = resp.state();
      delay_time = initial_delay;
      cout << Format(
          "Replication bootstrap in state $0",
          master::SysUniverseReplicationBootstrapEntryPB_State_Name(state)) << endl;
        } else {
      delay_time = std::min(max_delay_time, delay_time + delay_increment);
        }
        // Still processing, wait and then loop again.
        SleepFor(delay_time);
  }
}

Status ClusterAdminClient::SetupNamespaceReplicationWithBootstrap(
    const std::string& replication_id, const std::vector<std::string>& producer_addresses,
    const TypedNamespaceName& ns, bool transactional) {
  if (ns.db_type == YQL_DATABASE_CQL && transactional) {
        return STATUS(
            InvalidArgument, "Transactional replication is not supported for non-YSQL namespace");
  }

  master::SetupNamespaceReplicationWithBootstrapRequestPB req;
  master::SetupNamespaceReplicationWithBootstrapResponsePB resp;
  req.set_replication_id(replication_id);
  req.set_transactional(transactional);
  req.mutable_producer_namespace()->set_name(ns.name);
  req.mutable_producer_namespace()->set_database_type(ns.db_type);

  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
  for (const auto& addr : producer_addresses) {
        // HostPort::FromString() expects a default port.
        auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
        HostPortToPB(hp, req.add_producer_master_addresses());
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  auto setup_result_status =
      master_replication_proxy_->SetupNamespaceReplicationWithBootstrap(req, &resp, &rpc);

  setup_result_status = WaitForReplicationBootstrapToFinish(replication_id);

  if (resp.has_error()) {
        cout << "Error bootstrapping replication: " << resp.error().status().message() << endl;
        Status status_from_error = StatusFromPB(resp.error().status());

        return status_from_error;
  }

  if (!setup_result_status.ok()) {
        cout << "Error waiting for bootstrap replication to complete: "
             << setup_result_status.message().ToBuffer() << endl;
        return setup_result_status;
  }

  cout << "Replication bootstrap completed successfully, waiting for setup universe replication"
       << endl;

  setup_result_status = WaitForSetupUniverseReplicationToFinish(replication_id);

  if (resp.has_error()) {
        cout << "Error setting up universe replication: " << resp.error().status().message()
             << endl;
        Status status_from_error = StatusFromPB(resp.error().status());

        return status_from_error;
  }

  if (!setup_result_status.ok()) {
        cout << "Error waiting for universe replication setup to complete: "
             << setup_result_status.message().ToBuffer() << endl;
        return setup_result_status;
  }

  cout << "Replication setup successfully" << endl;

  return Status::OK();
}

Status ClusterAdminClient::SetupUniverseReplication(
    const string& replication_group_id, const vector<string>& producer_addresses,
    const vector<TableId>& tables, const vector<string>& producer_bootstrap_ids,
    bool transactional) {
  master::SetupUniverseReplicationRequestPB req;
  master::SetupUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id);
  req.set_transactional(transactional);

  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
  for (const auto& addr : producer_addresses) {
        // HostPort::FromString() expects a default port.
        auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
        HostPortToPB(hp, req.add_producer_master_addresses());
  }

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(tables.size()));
  for (const auto& table : tables) {
        req.add_producer_table_ids(table);
  }

  for (const auto& producer_bootstrap_id : producer_bootstrap_ids) {
        req.add_producer_bootstrap_ids(producer_bootstrap_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  auto setup_result_status = master_replication_proxy_->SetupUniverseReplication(req, &resp, &rpc);

  setup_result_status = WaitForSetupUniverseReplicationToFinish(replication_group_id);

  if (resp.has_error()) {
        cout << "Error setting up universe replication: " << resp.error().status().message()
             << endl;
        Status status_from_error = StatusFromPB(resp.error().status());

        return status_from_error;
  }

  // Clean up config files if setup fails to complete.
  if (!setup_result_status.ok()) {
        cout << "Error waiting for universe replication setup to complete: "
             << setup_result_status.message().ToBuffer() << endl;
        return setup_result_status;
  }

  cout << "Replication setup successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::DeleteUniverseReplication(
    const std::string& replication_group_id, bool ignore_errors) {
  master::DeleteUniverseReplicationRequestPB req;
  master::DeleteUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id);
  req.set_ignore_errors(ignore_errors);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->DeleteUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error deleting universe replication: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  if (resp.warnings().size() > 0) {
        cout << "Encountered the following warnings while running delete_universe_replication:"
             << endl;
        for (const auto& warning : resp.warnings()) {
      cout << " - " << warning.message() << endl;
        }
  }

  cout << "Replication deleted successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::AlterUniverseReplication(
    const std::string& replication_group_id, const std::vector<std::string>& producer_addresses,
    const std::vector<TableId>& add_tables, const std::vector<TableId>& remove_tables,
    const std::vector<std::string>& producer_bootstrap_ids_to_add,
    const std::string& new_replication_group_id, bool remove_table_ignore_errors) {
  master::AlterUniverseReplicationRequestPB req;
  master::AlterUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id);
  req.set_remove_table_ignore_errors(remove_table_ignore_errors);

  if (!producer_addresses.empty()) {
        req.mutable_producer_master_addresses()->Reserve(
            narrow_cast<int>(producer_addresses.size()));
        for (const auto& addr : producer_addresses) {
      // HostPort::FromString() expects a default port.
      auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
      HostPortToPB(hp, req.add_producer_master_addresses());
        }
  }

  if (!add_tables.empty()) {
        req.mutable_producer_table_ids_to_add()->Reserve(narrow_cast<int>(add_tables.size()));
        for (const auto& table : add_tables) {
      req.add_producer_table_ids_to_add(table);
        }

        if (!producer_bootstrap_ids_to_add.empty()) {
      // There msut be a bootstrap id for every table id.
      if (producer_bootstrap_ids_to_add.size() != add_tables.size()) {
        cout << "The number of bootstrap ids must equal the number of table ids. "
             << "Use separate alter commands if only some tables are being bootstrapped." << endl;
        return STATUS(InternalError, "Invalid number of bootstrap ids");
      }

      req.mutable_producer_bootstrap_ids_to_add()->Reserve(
          narrow_cast<int>(producer_bootstrap_ids_to_add.size()));
      for (const auto& bootstrap_id : producer_bootstrap_ids_to_add) {
        req.add_producer_bootstrap_ids_to_add(bootstrap_id);
      }
        }
  }

  if (!remove_tables.empty()) {
        req.mutable_producer_table_ids_to_remove()->Reserve(narrow_cast<int>(remove_tables.size()));
        for (const auto& table : remove_tables) {
      req.add_producer_table_ids_to_remove(table);
        }
  }

  if (!new_replication_group_id.empty()) {
    req.set_new_replication_group_id(new_replication_group_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->AlterUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error altering universe replication: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  if (!add_tables.empty()) {
        // If we are adding tables, then wait for the altered producer to be deleted (this happens
        // once it is merged with the original).
        RETURN_NOT_OK(WaitForSetupUniverseReplicationToFinish(
            xcluster::GetAlterReplicationGroupId(xcluster::ReplicationGroupId(replication_group_id))
                .ToString()));
  }

  cout << "Replication altered successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::ChangeXClusterRole(cdc::XClusterRole role) {
  master::ChangeXClusterRoleRequestPB req;
  master::ChangeXClusterRoleResponsePB resp;
  req.set_role(role);

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->ChangeXClusterRole(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error changing role: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Changed role successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::SetUniverseReplicationEnabled(
    const std::string& replication_group_id, bool is_enabled) {
  master::SetUniverseReplicationEnabledRequestPB req;
  master::SetUniverseReplicationEnabledResponsePB resp;
  req.set_replication_group_id(replication_group_id);
  req.set_is_enabled(is_enabled);
  const string toggle = (is_enabled ? "enabl" : "disabl");

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->SetUniverseReplicationEnabled(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error " << toggle << "ing "
             << "universe replication: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Replication " << toggle << "ed successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::PauseResumeXClusterProducerStreams(
    const std::vector<std::string>& stream_ids, bool is_paused) {
  master::PauseResumeXClusterProducerStreamsRequestPB req;
  master::PauseResumeXClusterProducerStreamsResponsePB resp;
  for (const auto& stream_id : stream_ids) {
        req.add_stream_ids(stream_id);
  }
  req.set_is_paused(is_paused);
  const auto toggle = (is_paused ? "paus" : "resum");

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->PauseResumeXClusterProducerStreams(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error " << toggle << "ing "
             << "replication: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Replication " << toggle << "ed successfully" << endl;
  return Status::OK();
}

Result<HostPort> ClusterAdminClient::GetFirstRpcAddressForTS() {
  RepeatedPtrField<ListTabletServersResponsePB::Entry> servers;
  RETURN_NOT_OK(ListTabletServers(&servers));
  for (const ListTabletServersResponsePB::Entry& server : servers) {
        if (server.has_registration() &&
            !server.registration().common().private_rpc_addresses().empty()) {
      return HostPortFromPB(server.registration().common().private_rpc_addresses(0));
        }
  }

  return STATUS(NotFound, "Didn't find a server registered with the Master");
}

Status ClusterAdminClient::BootstrapProducer(const vector<TableId>& table_ids) {
  HostPort ts_addr = VERIFY_RESULT(GetFirstRpcAddressForTS());
  auto cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(proxy_cache_.get(), ts_addr);

  cdc::BootstrapProducerRequestPB bootstrap_req;
  cdc::BootstrapProducerResponsePB bootstrap_resp;
  for (const auto& table_id : table_ids) {
        bootstrap_req.add_table_ids(table_id);
  }
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(std::max(timeout_.ToSeconds(), 120.0)));
  RETURN_NOT_OK(cdc_proxy->BootstrapProducer(bootstrap_req, &bootstrap_resp, &rpc));

  if (bootstrap_resp.has_error()) {
        cout << "Error bootstrapping consumer: " << bootstrap_resp.error().status().message()
             << endl;
        return StatusFromPB(bootstrap_resp.error().status());
  }

  if (implicit_cast<size_t>(bootstrap_resp.cdc_bootstrap_ids().size()) != table_ids.size()) {
        cout << "Received invalid number of bootstrap ids: " << bootstrap_resp.ShortDebugString();
        return STATUS(InternalError, "Invalid number of bootstrap ids");
  }

  int i = 0;
  for (const auto& bootstrap_id : bootstrap_resp.cdc_bootstrap_ids()) {
        cout << "table id: " << table_ids[i++] << ", CDC bootstrap id: " << bootstrap_id << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::WaitForReplicationDrain(
    const std::vector<xrepl::StreamId>& stream_ids, const string& target_time) {
  master::WaitForReplicationDrainRequestPB req;
  master::WaitForReplicationDrainResponsePB resp;
  for (const auto& stream_id : stream_ids) {
        req.add_stream_ids(stream_id.ToString());
  }
  // If target_time is not provided, it will be set to current time in the master API.
  if (!target_time.empty()) {
        auto result = HybridTime::ParseHybridTime(target_time);
        if (!result.ok()) {
      return STATUS(InvalidArgument, "Error parsing target_time: " + result.ToString());
        }
        req.set_target_time(result->GetPhysicalValueMicros());
  }
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->WaitForReplicationDrain(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error waiting for replication drain: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  std::unordered_map<xrepl::StreamId, std::vector<TabletId>> undrained_streams;
  for (const auto& stream_info : resp.undrained_stream_info()) {
        undrained_streams[VERIFY_RESULT(xrepl::StreamId::FromString(stream_info.stream_id()))]
            .push_back(stream_info.tablet_id());
  }
  if (!undrained_streams.empty()) {
        cout << "Found undrained replications:" << endl;
        for (const auto& stream_to_tablets : undrained_streams) {
      cout << "- Under Stream " << stream_to_tablets.first << ":" << endl;
      for (const auto& tablet_id : stream_to_tablets.second) {
        cout << "  - Tablet: " << tablet_id << endl;
      }
        }
  } else {
        cout << "All replications are caught-up." << endl;
  }
  return Status::OK();
}

Status ClusterAdminClient::SetupNSUniverseReplication(
    const std::string& replication_group_id,
    const std::vector<std::string>& producer_addresses,
    const TypedNamespaceName& producer_namespace) {
  switch (producer_namespace.db_type) {
        case YQL_DATABASE_CQL:
      break;
        case YQL_DATABASE_PGSQL:
      return STATUS(
          InvalidArgument, "YSQL not currently supported for namespace-level replication setup");
        default:
      return STATUS(InvalidArgument, "Unsupported namespace type");
  }

  master::SetupNSUniverseReplicationRequestPB req;
  master::SetupNSUniverseReplicationResponsePB resp;
  req.set_replication_group_id(replication_group_id);
  req.set_producer_ns_name(producer_namespace.name);
  req.set_producer_ns_type(producer_namespace.db_type);

  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_addresses.size()));
  for (const auto& addr : producer_addresses) {
        auto hp = VERIFY_RESULT(HostPort::FromString(addr, master::kMasterDefaultPort));
        HostPortToPB(hp, req.add_producer_master_addresses());
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->SetupNSUniverseReplication(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error setting up namespace-level universe replication: "
             << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << "Namespace-level replication setup successfully" << endl;
  return Status::OK();
}

Status ClusterAdminClient::GetReplicationInfo(const std::string& replication_group_id) {
  master::GetReplicationStatusRequestPB req;
  master::GetReplicationStatusResponsePB resp;

  if (!replication_group_id.empty()) {
        req.set_replication_group_id(replication_group_id);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetReplicationStatus(req, &resp, &rpc));

  if (resp.has_error()) {
        cout << "Error getting replication status: " << resp.error().status().message() << endl;
        return StatusFromPB(resp.error().status());
  }

  cout << resp.DebugString();
  return Status::OK();
}

Result<rapidjson::Document> ClusterAdminClient::GetXClusterSafeTime(bool include_lag_and_skew) {
  master::GetXClusterSafeTimeRequestPB req;
  master::GetXClusterSafeTimeResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(master_replication_proxy_->GetXClusterSafeTime(req, &resp, &rpc));

  if (resp.has_error()) {
    cout << "Error getting xCluster safe time values: " << resp.error().status().message() << endl;
    return StatusFromPB(resp.error().status());
  }

  rapidjson::Document document;
  document.SetArray();
  for (const auto& safe_time : resp.namespace_safe_times()) {
    rapidjson::Value json_entry(rapidjson::kObjectType);
    AddStringField("namespace_id", safe_time.namespace_id(), &json_entry, &document.GetAllocator());
    AddStringField(
        "namespace_name", safe_time.namespace_name(), &json_entry, &document.GetAllocator());
    const auto& safe_time_ht = HybridTime::FromPB(safe_time.safe_time_ht());
    AddStringField(
        "safe_time", HybridTimeToString(safe_time_ht), &json_entry, &document.GetAllocator());
    AddStringField(
        "safe_time_epoch", std::to_string(safe_time_ht.GetPhysicalValueMicros()), &json_entry,
        &document.GetAllocator());

    if (include_lag_and_skew) {
      // Print safe lag and skew in seconds with 2 decimal points.
      // Safe time lag is calculated as (current time - current safe time).
      std::string safe_time_lag = FormatDouble(
          MonoDelta::FromMicroseconds(safe_time.safe_time_lag()).ToMilliseconds() / 1000.0);
      AddStringField("safe_time_lag_sec", safe_time_lag, &json_entry, &document.GetAllocator());
      // Safe time skew is calculated as (safe time of most caught up tablet - safe time of
      // laggiest tablet).
      std::string safe_time_skew = FormatDouble(
          MonoDelta::FromMicroseconds(safe_time.safe_time_skew()).ToMilliseconds() / 1000.0);
      AddStringField("safe_time_skew_sec", safe_time_skew, &json_entry, &document.GetAllocator());
    }

    document.PushBack(json_entry, document.GetAllocator());
  }

  return document;
}

string RightPadToUuidWidth(const string &s) {
  return RightPadToWidth(s, kNumCharactersInUuid);
}

Result<TypedNamespaceName> ParseNamespaceName(const std::string& full_namespace_name,
                                              const YQLDatabase default_if_no_prefix) {
  const auto parts = SplitByDot(full_namespace_name);
  return ResolveNamespaceName(parts.prefix, parts.value, default_if_no_prefix);
}

void AddStringField(
    const char* name, const std::string& value, rapidjson::Value* out,
    rapidjson::Value::AllocatorType* allocator) {
  rapidjson::Value json_value(value.c_str(), *allocator);
  out->AddMember(rapidjson::StringRef(name), json_value, *allocator);
}

string HybridTimeToString(HybridTime ht) {
  return Timestamp(ht.GetPhysicalValueMicros()).ToHumanReadableTime();
}

}  // namespace tools
}  // namespace yb
