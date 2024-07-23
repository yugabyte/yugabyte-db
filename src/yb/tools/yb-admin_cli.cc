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

#include "yb/tools/yb-admin_cli.h"

#include <memory>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "yb/common/xcluster_util.h"
#include "yb/client/xcluster_client.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/json_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/util.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_defaults.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tools/yb-admin_util.h"
#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"
#include "yb/util/jsonwriter.h"

DEFINE_NON_RUNTIME_string(master_addresses, "localhost:7100",
    "Comma-separated list of YB Master server addresses");
DEFINE_NON_RUNTIME_string(init_master_addrs, "", "host:port of any yb-master in a cluster");
DEFINE_NON_RUNTIME_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");

// Command-specific flags
DEFINE_NON_RUNTIME_bool(exclude_dead, false, "Exclude dead tservers from output");

#define REGISTER_COMMAND(command_name) \
  Register(#command_name, command_name##_args, command_name##_action)

using std::cerr;
using std::endl;
using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

using yb::client::YBTableName;

using namespace std::placeholders;

namespace yb {
namespace tools {

const Status ClusterAdminCli::kInvalidArguments =
    STATUS(InvalidArgument, "Invalid arguments for operation");

namespace {

constexpr auto kBlacklistAdd = "ADD";
constexpr auto kBlacklistRemove = "REMOVE";
constexpr int32 kDefaultRpcPort = 9100;
const string kMinus = "minus";

const std::string namespace_expression =
    "<namespace>:\n [(ycql|ysql).]<namespace_name> (default ycql.)";
const std::string table_expression = "<table>:\n <namespace> <table_name> | tableid.<table_id>";
const std::string index_expression = "<index>:\n  <namespace> <index_name> | tableid.<index_id>";

Status GetUniverseConfig(ClusterAdminClient* client, const ClusterAdminCli::CLIArguments&) {
  RETURN_NOT_OK_PREPEND(client->GetUniverseConfig(), "Unable to get universe config");
  return Status::OK();
}

Status GetXClusterConfig(ClusterAdminClient* client, const ClusterAdminCli::CLIArguments&) {
  RETURN_NOT_OK_PREPEND(client->GetXClusterConfig(), "Unable to get xcluster config");
  return Status::OK();
}

Status ChangeBlacklist(
    ClusterAdminClient* client, const ClusterAdminCli::CLIArguments& args, bool blacklist_leader,
    const std::string& errStr) {
  if (args.size() < 2) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const auto change_type = args[0];
  if (change_type != kBlacklistAdd && change_type != kBlacklistRemove) {
    return ClusterAdminCli::kInvalidArguments;
  }
  std::vector<HostPort> hostports;
  for (const auto& arg : boost::make_iterator_range(args.begin() + 1, args.end())) {
    hostports.push_back(VERIFY_RESULT(HostPort::FromString(arg, kDefaultRpcPort)));
  }

  RETURN_NOT_OK_PREPEND(
      client->ChangeBlacklist(hostports, change_type == kBlacklistAdd, blacklist_leader), errStr);
  return Status::OK();
}

Status MasterLeaderStepDown(ClusterAdminClient* client, const ClusterAdminCli::CLIArguments& args) {
  const auto leader_uuid = VERIFY_RESULT(client->GetMasterLeaderUuid());
  return client->MasterLeaderStepDown(leader_uuid, args.size() > 0 ? args[0] : std::string());
}

Status LeaderStepDown(ClusterAdminClient* client, const ClusterAdminCli::CLIArguments& args) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  std::string dest_uuid = (args.size() > 1) ? args[1] : "";
  RETURN_NOT_OK_PREPEND(
      client->LeaderStepDownWithNewLeader(args[0], dest_uuid), "Unable to step down leader");
  return Status::OK();
}

bool IsEqCaseInsensitive(const string& check, const string& expected) {
  string upper_check, upper_expected;
  ToUpperCase(check, &upper_check);
  ToUpperCase(expected, &upper_expected);
  return upper_check == upper_expected;
}

template <class Enum>
Result<std::pair<std::optional<int>, EnumBitSet<Enum>>> GetValueAndFlags(
    const CLIArgumentsIterator& begin, const CLIArgumentsIterator& end,
    const AllEnumItemsIterable<Enum>& flags_list) {
  std::pair<std::optional<int>, EnumBitSet<Enum>> result;
  for (auto iter = begin; iter != end; iter = ++iter) {
    bool found_flag = false;
    for (auto flag : flags_list) {
      if (IsEqCaseInsensitive(*iter, ToString(flag))) {
        SCHECK(!result.second.Test(flag), InvalidArgument, Format("Duplicate flag: $0", flag));
        result.second.Set(flag);
        found_flag = true;
        break;
      }
    }
    if (found_flag) {
      continue;
    }

    SCHECK(
        !result.first, InvalidArgument, Format("Multiple values: $0 and $1", *result.first, *iter));

    result.first = VERIFY_RESULT(CheckedStoi(*iter));
  }

  return result;
}

YB_DEFINE_ENUM(AddIndexes, (ADD_INDEXES));

Result<pair<std::optional<int>, bool>> GetTimeoutAndAddIndexesFlag(
    CLIArgumentsIterator begin, const CLIArgumentsIterator& end) {
  auto temp_pair = VERIFY_RESULT(GetValueAndFlags(begin, end, AddIndexesList()));
  return std::make_pair(temp_pair.first, temp_pair.second.Test(AddIndexes::ADD_INDEXES));
}

YB_DEFINE_ENUM(ListTabletsFlags, (JSON)(INCLUDE_FOLLOWERS));

Status PrioritizedError(Status hi_pri_status, Status low_pri_status) {
  return hi_pri_status.ok() ? std::move(low_pri_status) : std::move(hi_pri_status);
}

template <class T, class Args>
Result<T> GetOptionalArg(const Args& args, size_t idx) {
  if (args.size() <= idx) {
    return T::Nil();
  }
  if (args.size() > idx + 1) {
    return STATUS_FORMAT(
        InvalidArgument, "Too many arguments for command, at most $0 expected, but $1 found",
        idx + 1, args.size());
  }
  return VERIFY_RESULT(T::FromString(args[idx]));
}

Status ListSnapshots(ClusterAdminClient* client, const EnumBitSet<ListSnapshotsFlag>& flags) {
  auto snapshot_response = VERIFY_RESULT(client->ListSnapshots(flags));

  rapidjson::Document document(rapidjson::kObjectType);
  bool json = flags.Test(ListSnapshotsFlag::JSON);

  if (snapshot_response.has_current_snapshot_id()) {
    if (json) {
      AddStringField(
          "current_snapshot_id", SnapshotIdToString(snapshot_response.current_snapshot_id()),
          &document, &document.GetAllocator());
    } else {
      std::cout << "Current snapshot id: "
                << SnapshotIdToString(snapshot_response.current_snapshot_id()) << std::endl;
    }
  }

  rapidjson::Value json_snapshots(rapidjson::kArrayType);
  if (!json) {
    if (snapshot_response.snapshots_size()) {
      // Using 2 tabs so that the header can be aligned to the time.
      std::cout << RightPadToUuidWidth("Snapshot UUID") << kColumnSep << "State" << kColumnSep
                << kColumnSep << "Creation Time" << std::endl;
    } else {
      std::cout << "No snapshots" << std::endl;
    }
  }

  for (master::SnapshotInfoPB& snapshot : *snapshot_response.mutable_snapshots()) {
    rapidjson::Value json_snapshot(rapidjson::kObjectType);
    if (json) {
      AddStringField(
          "id", SnapshotIdToString(snapshot.id()), &json_snapshot, &document.GetAllocator());
      const auto& entry = snapshot.entry();
      AddStringField(
          "state", master::SysSnapshotEntryPB::State_Name(entry.state()), &json_snapshot,
          &document.GetAllocator());
      AddStringField(
          "snapshot_time", HybridTimeToString(HybridTime::FromPB(entry.snapshot_hybrid_time())),
          &json_snapshot, &document.GetAllocator());
      AddStringField(
          "previous_snapshot_time",
          HybridTimeToString(HybridTime::FromPB(entry.previous_snapshot_hybrid_time())),
          &json_snapshot, &document.GetAllocator());
    } else {
      std::cout << SnapshotIdToString(snapshot.id()) << kColumnSep
                << master::SysSnapshotEntryPB::State_Name(snapshot.entry().state()) << kColumnSep
                << HybridTimeToString(HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time()))
                << std::endl;
    }

    // Not implemented in json mode.
    if (flags.Test(ListSnapshotsFlag::SHOW_DETAILS)) {
      for (master::SysRowEntry& entry : *snapshot.mutable_entry()->mutable_entries()) {
        string decoded_data;
        switch (entry.type()) {
          case master::SysRowEntryType::NAMESPACE: {
            auto meta =
                VERIFY_RESULT(pb_util::ParseFromSlice<master::SysNamespaceEntryPB>(entry.data()));
            meta.clear_transaction();
            decoded_data = JsonWriter::ToJson(meta, JsonWriter::COMPACT);
            break;
          }
          case master::SysRowEntryType::UDTYPE: {
            auto meta =
                VERIFY_RESULT(pb_util::ParseFromSlice<master::SysUDTypeEntryPB>(entry.data()));
            decoded_data = JsonWriter::ToJson(meta, JsonWriter::COMPACT);
            break;
          }
          case master::SysRowEntryType::TABLE: {
            auto meta =
                VERIFY_RESULT(pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data()));
            meta.clear_schema();
            meta.clear_partition_schema();
            meta.clear_index_info();
            meta.clear_indexes();
            meta.clear_transaction();
            decoded_data = JsonWriter::ToJson(meta, JsonWriter::COMPACT);
            break;
          }
          default:
            break;
        }

        if (!decoded_data.empty()) {
          entry.set_data("DATA");
          std::cout << kColumnSep
                    << StringReplace(
                           JsonWriter::ToJson(entry, JsonWriter::COMPACT), "\"DATA\"", decoded_data,
                           false)
                    << std::endl;
        }
      }
    }
    if (json) {
      json_snapshots.PushBack(json_snapshot, document.GetAllocator());
    }
  }

  if (json) {
    document.AddMember("snapshots", json_snapshots, document.GetAllocator());
    std::cout << common::PrettyWriteRapidJsonToString(document) << std::endl;
    return Status::OK();
  }

  auto restorations_result =
      VERIFY_RESULT(client->ListSnapshotRestorations(TxnSnapshotRestorationId::Nil()));
  if (restorations_result.restorations_size() == 0) {
    std::cout << "No snapshot restorations" << std::endl;
  } else if (flags.Test(ListSnapshotsFlag::NOT_SHOW_RESTORED)) {
    std::cout << "Not show fully RESTORED entries" << std::endl;
  }

  bool title_printed = false;
  for (const auto& restoration : restorations_result.restorations()) {
    if (!flags.Test(ListSnapshotsFlag::NOT_SHOW_RESTORED) ||
        restoration.entry().state() != master::SysSnapshotEntryPB::RESTORED) {
      if (!title_printed) {
        std::cout << RightPadToUuidWidth("Restoration UUID") << kColumnSep << "State" << std::endl;
        title_printed = true;
      }
      std::cout << TryFullyDecodeTxnSnapshotRestorationId(restoration.id()) << kColumnSep
                << master::SysSnapshotEntryPB::State_Name(restoration.entry().state()) << std::endl;
    }
  }

  return Status::OK();
}

Result<rapidjson::Document> ListSnapshotRestorations(
    ClusterAdminClient* client, const TxnSnapshotRestorationId& restoration_id) {
  auto resp = VERIFY_RESULT(client->ListSnapshotRestorations(restoration_id));
  rapidjson::Document result;
  result.SetObject();
  rapidjson::Value json_restorations(rapidjson::kArrayType);
  for (const auto& restoration : resp.restorations()) {
    rapidjson::Value json_restoration(rapidjson::kObjectType);
    AddStringField(
        "id", VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(restoration.id())).ToString(),
        &json_restoration, &result.GetAllocator());
    AddStringField(
        "snapshot_id",
        VERIFY_RESULT(FullyDecodeTxnSnapshotId(restoration.entry().snapshot_id())).ToString(),
        &json_restoration, &result.GetAllocator());
    AddStringField(
        "state", master::SysSnapshotEntryPB_State_Name(restoration.entry().state()),
        &json_restoration, &result.GetAllocator());
    json_restorations.PushBack(json_restoration, result.GetAllocator());
  }
  result.AddMember("restorations", json_restorations, result.GetAllocator());
  return result;
}

Status ImportSnapshot(
    ClusterAdminClient* client, const ClusterAdminCli::CLIArguments& args, bool selective) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  string filename = args[0];
  size_t num_tables = 0;
  TypedNamespaceName keyspace;
  vector<YBTableName> tables;

  if (args.size() >= 2) {
    keyspace = VERIFY_RESULT(ParseNamespaceName(args[1]));
    num_tables = args.size() - 2;

    if (num_tables > 0) {
      LOG_IF(DFATAL, keyspace.name.empty()) << "Uninitialized keyspace: " << keyspace.name;
      tables.reserve(num_tables);

      for (size_t i = 0; i < num_tables; ++i) {
        tables.push_back(YBTableName(keyspace.db_type, keyspace.name, args[2 + i]));
      }
    }
  }

  string msg = num_tables > 0 ? Format(
                                    "Unable to import tables $0 from snapshot meta file $1",
                                    yb::ToString(tables), filename)
                              : Format("Unable to import snapshot meta file $0", filename);

  RETURN_NOT_OK_PREPEND(client->ImportSnapshotMetaFile(filename, keyspace, tables, selective), msg);
  return Status::OK();
}

Status PrintJsonResult(Result<rapidjson::Document>&& action_result) {
  RETURN_NOT_OK(action_result);
  auto& result = *action_result;
  std::cout << common::PrettyWriteRapidJsonToString(result) << std::endl;
  return Status::OK();
}

}  // namespace

std::string ClusterAdminCli::GetArgumentExpressions(const std::string& usage_arguments) {
  std::string expressions;
  std::stringstream ss(usage_arguments);
  std::string next_argument;
  while (ss >> next_argument) {
    if (next_argument == "<namespace>" || next_argument == "<source_namespace>") {
      expressions += namespace_expression + '\n';
    } else if (next_argument == "<table>") {
      expressions += table_expression + '\n';
    } else if (next_argument == "<index>") {
      expressions += index_expression + '\n';
    }
  }
  return expressions.empty() ? "" : "Definitions: " + expressions;
}

Status ClusterAdminCli::RunCommand(
    const Command& command, const CLIArguments& command_args, const std::string& program_name) {
  auto s = command.action_(command_args, client_.get());
  if (!s.ok()) {
    if (s.IsRemoteError() && s.ToString().find("rpc error 2")) {
      cerr << "The cluster doesn't support " << command.name_ << ": " << s << std::endl;
    } else {
      cerr << "Error running " << command.name_ << ": " << s << endl;
      if (s.IsInvalidArgument()) {
        cerr << Format("Usage: $0 $1 $2", program_name, command.name_, command.usage_arguments_)
             << endl
             << GetArgumentExpressions(command.usage_arguments_);
      }
    }
    return STATUS(RuntimeError, "Error running command");
  }
  return Status::OK();
}

Status ClusterAdminCli::Run(int argc, char** argv) {
  const string prog_name = argv[0];
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 2;
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(prog_name.c_str());

  const string addrs = FLAGS_master_addresses;
  if (!FLAGS_init_master_addrs.empty()) {
    std::vector<HostPort> init_master_addrs;
    RETURN_NOT_OK(HostPort::ParseStrings(
        FLAGS_init_master_addrs, master::kMasterDefaultPort, &init_master_addrs));
    client_.reset(new ClusterAdminClient(
        init_master_addrs[0], MonoDelta::FromMilliseconds(FLAGS_timeout_ms)));
  } else {
    client_.reset(new ClusterAdminClient(addrs, MonoDelta::FromMilliseconds(FLAGS_timeout_ms)));
  }

  RegisterCommandHandlers();
  SetUsage(prog_name);

  CLIArguments args;
  for (int i = 0; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  if (args.size() < 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  // Find operation handler by operation name.
  const string op = args[1];
  auto cmd = command_indexes_.find(op);

  if (cmd == command_indexes_.end()) {
    cerr << "Invalid operation: " << op << endl;
    return ClusterAdminCli::kInvalidArguments;
  }

  // Init client.
  Status s = client_->Init();

  if (PREDICT_FALSE(!s.ok())) {
    cerr << s.CloneAndPrepend(
                 "Unable to establish connection to leader master at [" + addrs +
                 "]."
                 " Please verify the addresses and check if server is up, or if you're"
                 " missing --certs_dir_name.\n\n")
                .ToString()
         << endl;
    return STATUS(RuntimeError, "Error connecting to cluster");
  }

  CLIArguments command_args(args.begin() + 2, args.end());
  auto& command = commands_[cmd->second];
  return RunCommand(command, command_args, args[0]);
}

void ClusterAdminCli::Register(string&& cmd_name, const std::string& cmd_args, Action&& action) {
  command_indexes_[cmd_name] = commands_.size();
  commands_.push_back({std::move(cmd_name), cmd_args, std::move(action)});
}

void ClusterAdminCli::SetUsage(const string& prog_name) {
  ostringstream str;

  str << prog_name << " [-master_addresses server1:port,server2:port,server3:port,...] "
      << " [-timeout_ms <millisec>] [-certs_dir_name <dir_name>] <operation>" << endl
      << "<operation> must be one of:" << endl;

  for (size_t i = 0; i < commands_.size(); ++i) {
    str << ' ' << i + 1 << ". " << commands_[i].name_
        << (commands_[i].usage_arguments_.empty() ? "" : " ") << commands_[i].usage_arguments_
        << endl;
  }

  str << endl;
  str << namespace_expression << endl;
  str << table_expression << endl;
  str << index_expression << endl;

  google::SetUsageMessage(str.str());
}

namespace {

static const auto kIncludeDBType = "include_db_type";
static const auto kIncludeTableId = "include_table_id";
static const auto kIncludeTableType = "include_table_type";

const auto change_config_args =
    "<tablet_id> <ADD_SERVER|REMOVE_SERVER> <peer_uuid> [PRE_VOTER|PRE_OBSERVER]";
Status change_config_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string tablet_id = args[0];
  const string change_type = args[1];
  const string peer_uuid = args[2];
  boost::optional<string> member_type;
  if (args.size() > 3) {
    member_type = args[3];
  }
  RETURN_NOT_OK_PREPEND(
      client->ChangeConfig(tablet_id, change_type, peer_uuid, member_type),
      "Unable to change config");
  return Status::OK();
}

const auto list_tablet_servers_args = "<tablet_id>";
Status list_tablet_servers_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string tablet_id = args[0];
  RETURN_NOT_OK_PREPEND(
      client->ListPerTabletTabletServers(tablet_id),
      Format("Unable to list tablet servers of tablet $0", tablet_id));
  return Status::OK();
}

const auto backfill_indexes_for_table_args = "<table>";
Status backfill_indexes_for_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK_PREPEND(
      client->LaunchBackfillIndexForTable(table_name),
      yb::Format("Unable to launch backfill for indexes in $0", table_name));
  return Status::OK();
}

const auto list_tables_args =
    Format("[$0] [$1] [$2]", kIncludeDBType, kIncludeTableId, kIncludeTableType);
Status list_tables_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  bool include_db_type = false;
  bool include_table_id = false;
  bool include_table_type = false;
  const std::array<std::pair<const char*, bool*>, 3> flags{
      std::make_pair(kIncludeDBType, &include_db_type),
      std::make_pair(kIncludeTableId, &include_table_id),
      std::make_pair(kIncludeTableType, &include_table_type)};
  for (const auto& arg : args) {
    for (const auto& flag : flags) {
      if (flag.first == arg) {
        *flag.second = true;
      }
    }
  }
  RETURN_NOT_OK_PREPEND(
      client->ListTables(include_db_type, include_table_id, include_table_type),
      "Unable to list tables");
  return Status::OK();
}

// Deprecated list_tables commands with arguments should be used instead.
const auto list_tables_with_db_types_args = "";
Status list_tables_with_db_types_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(
      client->ListTables(
          true /* include_db_type */, false /* include_table_id*/, false /* include_table_type*/),
      "Unable to list tables");
  return Status::OK();
}

const auto list_tablets_args =
    "<table> [<max_tablets>] (default 10, set 0 for max) [JSON] [include_followers]";
Status list_tablets_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  std::pair<std::optional<int>, EnumBitSet<ListTabletsFlags>> arguments;
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(), [&arguments](auto i, const auto& end) -> Status {
        arguments = VERIFY_RESULT(GetValueAndFlags(i, end, ListTabletsFlagsList()));
        return Status::OK();
      }));
  RETURN_NOT_OK_PREPEND(
      client->ListTablets(
          table_name, arguments.first.value_or(10) /*max tablets*/,
          arguments.second.Test(ListTabletsFlags::JSON),
          arguments.second.Test(ListTabletsFlags::INCLUDE_FOLLOWERS)),
      Format("Unable to list tablets of table $0", table_name));
  return Status::OK();
}

const auto modify_table_placement_info_args =
    "<table> <placement_info> <replication_factor> [<placement_uuid>]";
Status modify_table_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 3 || args.size() > 5) {
    return ClusterAdminCli::kInvalidArguments;
  }
  std::string placement_info;
  int rf = -1;
  std::string placement_uuid;
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(),
      [&placement_info, &rf, &placement_uuid](auto i, const auto& end) -> Status {
        // Get placement info.
        placement_info = *i;
        i = std::next(i);
        // Get replication factor.
        rf = VERIFY_RESULT(CheckedStoi(*i));
        i = std::next(i);
        // Get optional placement uuid.
        if (i != end) {
          placement_uuid = *i;
        }
        return Status::OK();
      }));
  RETURN_NOT_OK_PREPEND(
      client->ModifyTablePlacementInfo(table_name, placement_info, rf, placement_uuid),
      Format("Unable to modify placement info for table $0", table_name.ToString()));
  return Status::OK();
}

const auto modify_placement_info_args = "<placement_info> <replication_factor> [<placement_uuid>]";
Status modify_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2 && args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  int rf = boost::lexical_cast<int>(args[1]);
  string placement_uuid = args.size() == 3 ? args[2] : "";
  RETURN_NOT_OK_PREPEND(
      client->ModifyPlacementInfo(args[0], rf, placement_uuid),
      Format("Unable to modify placement info."));
  return Status::OK();
}

const auto clear_placement_info_args = "";
Status clear_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(client->ClearPlacementInfo(), Format("Unable to clear placement info."));
  return Status::OK();
}

const auto add_read_replica_placement_info_args =
    "<placement_info> <replication_factor> [<placement_uuid>]";
Status add_read_replica_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2 && args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  int rf = boost::lexical_cast<int>(args[1]);
  string placement_uuid = args.size() == 3 ? args[2] : "";
  RETURN_NOT_OK_PREPEND(
      client->AddReadReplicaPlacementInfo(args[0], rf, placement_uuid),
      Format("Unable to add read replica placement info."));
  return Status::OK();
}

const auto modify_read_replica_placement_info_args =
    "<placement_info> <replication_factor> [<placement_uuid>]";
Status modify_read_replica_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2 && args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  int rf = boost::lexical_cast<int>(args[1]);
  string placement_uuid = args.size() == 3 ? args[2] : "";
  RETURN_NOT_OK_PREPEND(
      client->ModifyReadReplicaPlacementInfo(placement_uuid, args[0], rf),
      Format("Unable to modify read replica placement info."));
  return Status::OK();
}

const auto delete_read_replica_placement_info_args = "";
Status delete_read_replica_placement_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteReadReplicaPlacementInfo(),
      Format("Unable to delete read replica placement info."));
  return Status::OK();
}

const auto list_namespaces_args = "[INCLUDE_NONRUNNING] (default false)";
Status list_namespaces_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
    bool include_nonrunning = false;
    if (args.size() > 0) {
      if (IsEqCaseInsensitive(args[0], "INCLUDE_NONRUNNING")) {
        include_nonrunning = true;
      } else {
        return ClusterAdminCli::kInvalidArguments;
      }
    }
    RETURN_NOT_OK_PREPEND(
        client->ListAllNamespaces(include_nonrunning), "Unable to list namespaces");
    return Status::OK();
}

const auto delete_namespace_args = "<namespace>";
Status delete_namespace_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  auto namespace_name = VERIFY_RESULT(ParseNamespaceName(args[0]));
  RETURN_NOT_OK_PREPEND(
      client->DeleteNamespace(namespace_name),
      Format("Unable to delete namespace $0", namespace_name.name));
  return Status::OK();
}

const auto delete_namespace_by_id_args = "<namespace_id>";
Status delete_namespace_by_id_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteNamespaceById(args[0]), Format("Unable to delete namespace $0", args[0]));
  return Status::OK();
}

const auto delete_table_args = "<table>";
Status delete_table_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK_PREPEND(
      client->DeleteTable(table_name), Format("Unable to delete table $0", table_name.ToString()));
  return Status::OK();
}

const auto delete_table_by_id_args = "<table_id>";
Status delete_table_by_id_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteTableById(args[0]), Format("Unable to delete table $0", args[0]));
  return Status::OK();
}

const auto delete_index_args = "<index>";
Status delete_index_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK_PREPEND(
      client->DeleteIndex(table_name), Format("Unable to delete index $0", table_name.ToString()));
  return Status::OK();
}

const auto delete_index_by_id_args = "<index_id>";
Status delete_index_by_id_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteIndexById(args[0]), Format("Unable to delete index $0", args[0]));
  return Status::OK();
}

const auto flush_table_args =
    "<table> [<timeout_in_seconds>] (default 20) [ADD_INDEXES] (default false)";
Status flush_table_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  bool add_indexes = false;
  std::optional<int> timeout_secs;
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(),
      [&add_indexes, &timeout_secs](auto i, const auto& end) -> Status {
        std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(GetTimeoutAndAddIndexesFlag(i, end));
        return Status::OK();
      }));
  RETURN_NOT_OK_PREPEND(
      client->FlushTables(
          {table_name}, add_indexes, timeout_secs.value_or(20), false /* is_compaction */),
      Format("Unable to flush table $0", table_name.ToString()));
  return Status::OK();
}

const auto flush_table_by_id_args =
    "<table_id> [<timeout_in_seconds>] (default 20) [ADD_INDEXES] (default false)";
Status flush_table_by_id_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  std::optional<int> timeout_secs;
  bool add_indexes = false;
  std::tie(timeout_secs, add_indexes) =
      VERIFY_RESULT(GetTimeoutAndAddIndexesFlag(args.begin() + 1, args.end()));
  RETURN_NOT_OK_PREPEND(
      client->FlushTablesById(
          {args[0]}, add_indexes, timeout_secs.value_or(20), false /* is_compaction */),
      Format("Unable to flush table $0", args[0]));
  return Status::OK();
}

const auto flush_sys_catalog_args = "";
Status flush_sys_catalog_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->FlushSysCatalog(), "Unable to flush table sys_catalog");
  return Status::OK();
}

const auto compact_sys_catalog_args = "";
Status compact_sys_catalog_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->CompactSysCatalog(), "Unable to compact table sys_catalog");
  return Status::OK();
}

const auto compact_table_args =
    "<table> [<timeout_in_seconds>] (default 20) [ADD_INDEXES] (default false)";
Status compact_table_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  bool add_indexes = false;
  std::optional<int> timeout_secs;
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(),
      [&add_indexes, &timeout_secs](auto i, const auto& end) -> Status {
        std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(GetTimeoutAndAddIndexesFlag(i, end));
        return Status::OK();
      }));
  // We use the same FlushTables RPC to trigger compaction.
  RETURN_NOT_OK_PREPEND(
      client->FlushTables(
          {table_name}, add_indexes, timeout_secs.value_or(20), true /* is_compaction */),
      Format("Unable to compact table $0", table_name.ToString()));
  return Status::OK();
}

const auto compact_table_by_id_args =
    "<table_id> [<timeout_in_seconds>] (default 20) [ADD_INDEXES] (default false)";
Status compact_table_by_id_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const auto& [timeout_secs, add_indexes] =
      VERIFY_RESULT(GetTimeoutAndAddIndexesFlag(args.begin() + 1, args.end()));
  // We use the same FlushTables RPC to trigger compaction.
  RETURN_NOT_OK_PREPEND(
      client->FlushTablesById(
          {args[0]}, add_indexes, timeout_secs.value_or(20), true /* is_compaction */),
      Format("Unable to compact table $0", args[0]));
  return Status::OK();
}

const auto compaction_status_args = "<table> [show_tablets] (default false)";
Status compaction_status_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.empty()) {
    return ClusterAdminCli::kInvalidArguments;
  }

  bool show_tablets = false;

  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(), [&show_tablets](auto i, const auto& end) -> Status {
        if (i == end) {
          return Status::OK();
        }

        if (*i != "show_tablets" || i + 1 != end) {
          return ClusterAdminCli::kInvalidArguments;
        }
        show_tablets = true;
        return Status::OK();
      }));

  RETURN_NOT_OK_PREPEND(
      client->CompactionStatus(table_name, show_tablets),
      Format("Unable to get compaction status of table $0", table_name.ToString()));
  return Status::OK();
}

const auto list_all_tablet_servers_args = "";
Status list_all_tablet_servers_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(
      client->ListAllTabletServers(FLAGS_exclude_dead), "Unable to list tablet servers");
  return Status::OK();
}

const auto list_all_masters_args = "";
Status list_all_masters_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->ListAllMasters(), "Unable to list masters");
  return Status::OK();
}

const auto change_master_config_args = "<ADD_SERVER|REMOVE_SERVER> <ip_addr> <port> [<uuid>]";
Status change_master_config_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  uint16_t new_port = 0;
  string new_host;

  if (args.size() < 3 || args.size() > 4) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string change_type = args[0];
  if (change_type != "ADD_SERVER" && change_type != "REMOVE_SERVER") {
    return ClusterAdminCli::kInvalidArguments;
  }

  new_host = args[1];
  new_port = VERIFY_RESULT(CheckedStoi(args[2]));

  string given_uuid;
  if (args.size() == 4) {
    given_uuid = args[3];
  }
  RETURN_NOT_OK_PREPEND(
      client->ChangeMasterConfig(change_type, new_host, new_port, given_uuid),
      "Unable to change master config");
  return Status::OK();
}

const auto dump_masters_state_args = "[CONSOLE]";
Status dump_masters_state_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  bool to_console = false;
  if (args.size() > 0) {
    if (IsEqCaseInsensitive(args[0], "CONSOLE")) {
      to_console = true;
    } else {
      return ClusterAdminCli::kInvalidArguments;
    }
  }
  RETURN_NOT_OK_PREPEND(client->DumpMasterState(to_console), "Unable to dump master state");
  return Status::OK();
}

const auto list_tablet_server_log_locations_args = "";
Status list_tablet_server_log_locations_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(
      client->ListTabletServersLogLocations(), "Unable to list tablet server log locations");
  return Status::OK();
}

const auto list_tablets_for_tablet_server_args = "<ts_uuid>";
Status list_tablets_for_tablet_server_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string& ts_uuid = args[0];
  RETURN_NOT_OK_PREPEND(
      client->ListTabletsForTabletServer(ts_uuid), "Unable to list tablet server tablets");
  return Status::OK();
}

const auto set_load_balancer_enabled_args = "(0|1)";
Status set_load_balancer_enabled_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const bool is_enabled = VERIFY_RESULT(CheckedStoi(args[0])) != 0;
  RETURN_NOT_OK_PREPEND(
      client->SetLoadBalancerEnabled(is_enabled), "Unable to change load balancer state");
  return Status::OK();
}

const auto get_load_balancer_state_args = "";
Status get_load_balancer_state_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0) {
    return ClusterAdminCli::kInvalidArguments;
  }

  RETURN_NOT_OK_PREPEND(client->GetLoadBalancerState(), "Unable to get the load balancer state");
  return Status::OK();
}

const auto get_load_move_completion_args = "";
Status get_load_move_completion_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->GetLoadMoveCompletion(), "Unable to get load completion");
  return Status::OK();
}

const auto get_leader_blacklist_completion_args = "";
Status get_leader_blacklist_completion_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(
      client->GetLeaderBlacklistCompletion(), "Unable to get leader blacklist completion");
  return Status::OK();
}

const auto get_is_load_balancer_idle_args = "";
Status get_is_load_balancer_idle_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->GetIsLoadBalancerIdle(), "Unable to get is load balancer idle");
  return Status::OK();
}

const auto list_leader_counts_args = "<table>";
Status list_leader_counts_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK_PREPEND(client->ListLeaderCounts(table_name), "Unable to get leader counts");
  return Status::OK();
}

const auto setup_redis_table_args = "";
Status setup_redis_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->SetupRedisTable(), "Unable to setup Redis keyspace and table");
  return Status::OK();
}

const auto drop_redis_table_args = "";
Status drop_redis_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->DropRedisTable(), "Unable to drop Redis table");
  return Status::OK();
}

const auto get_universe_config_args = "";
Status get_universe_config_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return GetUniverseConfig(client, args);
}

const auto get_xcluster_info_args = "";
Status get_xcluster_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return GetXClusterConfig(client, args);
}

const auto change_blacklist_args =
    Format("<$0|$1> <ip_addr>:<port> [<ip_addr>:<port>]...", kBlacklistAdd, kBlacklistRemove);
Status change_blacklist_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return ChangeBlacklist(client, args, false, "Unable to change blacklist");
}

const auto change_leader_blacklist_args =
    Format("<$0|$1> <ip_addr>:<port> [<ip_addr>:<port>]...", kBlacklistAdd, kBlacklistRemove);
Status change_leader_blacklist_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return ChangeBlacklist(client, args, true, "Unable to change leader blacklist");
}

const auto master_leader_stepdown_args = "[<dest_uuid>]";
Status master_leader_stepdown_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return MasterLeaderStepDown(client, args);
}

const auto leader_stepdown_args = "<tablet_id> [<dest_ts_uuid>]";
Status leader_stepdown_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return LeaderStepDown(client, args);
}

const auto split_tablet_args = "<tablet_id>";
Status split_tablet_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string tablet_id = args[0];
  RETURN_NOT_OK_PREPEND(
      client->SplitTablet(tablet_id), Format("Unable to start split of tablet $0", tablet_id));
  return Status::OK();
}

const auto disable_tablet_splitting_args = "<disable_duration_ms> <feature_name>";
Status disable_tablet_splitting_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 2) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const int64_t disable_duration_ms = VERIFY_RESULT(CheckedStoll(args[0]));
  const std::string feature_name = args[1];
  RETURN_NOT_OK_PREPEND(
      client->DisableTabletSplitting(disable_duration_ms, feature_name),
      Format("Unable to disable tablet splitting for $0", feature_name));
  return Status::OK();
}

const auto is_tablet_splitting_complete_args = "[wait_for_parent_deletion] (default false)";
Status is_tablet_splitting_complete_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  bool wait_for_parent_deletion = false;
  if (args.size() > 0) {
    if (IsEqCaseInsensitive(args[0], "wait_for_parent_deletion")) {
      wait_for_parent_deletion = true;
    } else {
      return ClusterAdminCli::kInvalidArguments;
    }
  }
  RETURN_NOT_OK_PREPEND(
      client->IsTabletSplittingComplete(wait_for_parent_deletion),
      "Unable to check if tablet splitting is complete");
  return Status::OK();
}

const auto create_transaction_table_args = "<table_name>";
Status create_transaction_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string table_name = args[0];
  RETURN_NOT_OK_PREPEND(
      client->CreateTransactionsStatusTable(table_name),
      Format("Unable to create transaction table named $0", table_name));
  return Status::OK();
}

const auto add_transaction_tablet_args = "<table_id>";
Status add_transaction_tablet_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string table_id = args[0];
  RETURN_NOT_OK_PREPEND(
      client->AddTransactionStatusTablet(table_id),
      Format("Unable to add a tablet for transaction table $0", table_id));
  return Status::OK();
}

const auto ysql_catalog_version_args = "";
Status ysql_catalog_version_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->GetYsqlCatalogVersion(), "Unable to get catalog version");
  return Status::OK();
}

const auto ddl_log_args = "";
Status ddl_log_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 0, 0));
  return PrintJsonResult(client->DdlLog());
}

const auto upgrade_ysql_args = "[use_single_connection] (default false)";
Status upgrade_ysql_action(const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  // Use just one simultaneous connection for YSQL upgrade.
  // This is much slower but does not incur overhead for each database.
  bool use_single_connection = false;
  if (args.size() > 0) {
    if (IsEqCaseInsensitive(args[0], "use_single_connection")) {
      use_single_connection = true;
    } else {
      return ClusterAdminCli::kInvalidArguments;
    }
  }

  RETURN_NOT_OK_PREPEND(
      client->UpgradeYsql(use_single_connection), "Unable to upgrade YSQL cluster");
  return Status::OK();
}

// Today we have a weird pattern recognization for table name.
// The expected input argument for the <table> is:
// <db type>.<namespace> <table name>
// (with a space in between).
// So the expected arguement size is 3 (= 2 for the table name + 1 for the retention
// time).
const auto set_wal_retention_secs_args = "<table> <seconds>";
Status set_wal_retention_secs_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 3, 3));

  uint32_t wal_ret_secs = 0;
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
      client, args.begin(), args.end(), [&wal_ret_secs](auto i, const auto& end) -> Status {
        if (PREDICT_FALSE(i == end)) {
          return STATUS(InvalidArgument, "Table name not found in the command");
        }

        const auto raw_time = VERIFY_RESULT(CheckedStoi(*i));
        if (raw_time < 0) {
          return STATUS(
              InvalidArgument, "WAL retention time must be non-negative integer in seconds");
        }
        wal_ret_secs = static_cast<uint32_t>(raw_time);
        return Status::OK();
      }));
  RETURN_NOT_OK_PREPEND(
      client->SetWalRetentionSecs(table_name, wal_ret_secs),
      "Unable to set WAL retention time (sec) for the cluster");
  return Status::OK();
}

const auto get_wal_retention_secs_args = "<table>";
Status get_wal_retention_secs_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 2, 2));
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK(client->GetWalRetentionSecs(table_name));
  return Status::OK();
}

const auto get_auto_flags_config_args = "";
Status get_auto_flags_config_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->GetAutoFlagsConfig(), "Unable to get AutoFlags config");
  return Status::OK();
}

const auto promote_auto_flags_args =
    "[<max_flags_class> (default kExternal) [<promote_non_runtime_flags> (default true) [force]]]";
Status promote_auto_flags_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 3) {
    return ClusterAdminCli::kInvalidArguments;
  }

  AutoFlagClass max_flag_class = AutoFlagClass::kExternal;
  bool promote_non_runtime_flags = true;
  bool force = false;

  if (args.size() > 0) {
    max_flag_class = VERIFY_RESULT_PREPEND(
        ParseEnumInsensitive<AutoFlagClass>(args[0]), "Invalid value provided for max_flags_class");
  }

  if (args.size() > 1) {
    if (IsEqCaseInsensitive(args[1], "false")) {
      promote_non_runtime_flags = false;
    } else if (!IsEqCaseInsensitive(args[1], "true")) {
      return STATUS(InvalidArgument, "Invalid value provided for promote_non_runtime_flags");
    }
  }

  if (args.size() > 2) {
    if (IsEqCaseInsensitive(args[2], "force")) {
      force = true;
    } else {
      return ClusterAdminCli::kInvalidArguments;
    }
  }

  RETURN_NOT_OK_PREPEND(
      client->PromoteAutoFlags(ToString(max_flag_class), promote_non_runtime_flags, force),
      "Unable to promote AutoFlags");
  return Status::OK();
}

const auto rollback_auto_flags_args = "<rollback_version>";
Status rollback_auto_flags_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const int64_t rollback_version = VERIFY_RESULT(CheckedStoll(args[0]));
  SCHECK_LT(
      rollback_version, std::numeric_limits<uint32_t>::max(), InvalidArgument,
      "rollback_version exceeds bounds");

  RETURN_NOT_OK_PREPEND(
      client->RollbackAutoFlags(static_cast<uint32_t>(rollback_version)),
      "Unable to Rollback AutoFlags");
  return Status::OK();
}

const auto promote_single_auto_flag_args = "<process_name> <auto_flag_name>";
Status promote_single_auto_flag_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto& process_name = args[0];
  auto& auto_flag_name = args[1];

  RETURN_NOT_OK_PREPEND(
      client->PromoteSingleAutoFlag(process_name, auto_flag_name), "Unable to Promote AutoFlag");
  return Status::OK();
}

const auto demote_single_auto_flag_args = "<process_name> <auto_flag_name> [force]";
Status demote_single_auto_flag_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 3) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto& process_name = args[0];
  auto& auto_flag_name = args[1];
  if (args.size() == 3 && !IsEqCaseInsensitive(args[2], "force")) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const bool force = args.size() == 3;

  if (!force) {
    std::cout
        << "WARNING: Demotion of AutoFlags is dangerous and can lead to silent corruptions and "
           "data loss!";
    std::cout << "Are you sure you want to demote the flag '" << auto_flag_name
              << "' belonging to process '" << process_name << "'? (y/N)?";
    std::string answer;
    std::cin >> answer;
    SCHECK(answer == "y" || answer == "Y", InvalidArgument, "demote_single_auto_flag aborted");
  }

  RETURN_NOT_OK_PREPEND(
      client->DemoteSingleAutoFlag(process_name, auto_flag_name), "Unable to Demote AutoFlag");
  return Status::OK();
}

std::string GetListSnapshotsFlagList() {
  std::string options = "";
  for (auto flag : ListSnapshotsFlagList()) {
    options += Format(" [$0]", flag);
  }
  return options;
}
const auto list_snapshots_args = GetListSnapshotsFlagList();
Status list_snapshots_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  EnumBitSet<ListSnapshotsFlag> flags;

  for (size_t i = 0; i < args.size(); ++i) {
    std::string uppercase_flag;
    ToUpperCase(args[i], &uppercase_flag);

    bool found = false;
    for (auto flag : ListSnapshotsFlagList()) {
      if (uppercase_flag == ToString(flag)) {
        flags.Set(flag);
        found = true;
        break;
      }
    }
    if (!found) {
      return STATUS_FORMAT(InvalidArgument, "Wrong flag: $0", args[i]);
    }
  }

  RETURN_NOT_OK_PREPEND(ListSnapshots(client, flags), "Unable to list snapshots");
  return Status::OK();
}

const auto create_snapshot_args =
    "<table> [<table>]... [<flush_timeout_in_seconds>] (default 60, set 0 to skip flushing) "
    "[<retention_duration_hours>] (set a <= 0 value to retain the snapshot forever. If not "
    "specified then takes the default value controlled by gflag default_snapshot_retention_hours) "
    "[skip_indexes]";
Status create_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  int timeout_secs = 60;
  std::optional<int32_t> retention_duration_hours;
  bool timeout_set = false;
  bool skip_indexes = false;
  const auto tables = VERIFY_RESULT(ResolveTableNames(
      client, args.begin(), args.end(), [&](auto i, const auto& end) -> Status {
        for (auto curr_it = i; curr_it != end; ++curr_it) {
          if (IsEqCaseInsensitive(*curr_it, "skip_indexes")) {
            skip_indexes = true;
            continue;
          }
          if (!timeout_set) {
            timeout_secs = VERIFY_RESULT(CheckedStoi(*curr_it));
            timeout_set = true;
          } else if (!retention_duration_hours) {
            retention_duration_hours = VERIFY_RESULT(CheckedStoi(*curr_it));
          } else {
            return ClusterAdminCli::kInvalidArguments;
          }
        }
        return Status::OK();
      }));

  for (auto table : tables) {
    if (table.is_cql_namespace() && table.is_system()) {
      return STATUS(
          InvalidArgument, "Cannot create snapshot of YCQL system table", table.table_name());
    }
    if (table.is_pgsql_namespace()) {
      return STATUS(
          InvalidArgument,
          "Cannot create snapshot of individual YSQL tables. Only database level is supported");
    }
  }

  RETURN_NOT_OK_PREPEND(
      client->CreateSnapshot(tables, retention_duration_hours,
                             !skip_indexes, timeout_secs),
      Format("Unable to create snapshot of tables: $0", yb::ToString(tables)));
  return Status::OK();
}

const auto list_snapshot_restorations_args = "[<restoration_id>]";
Status list_snapshot_restorations_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  auto restoration_id = VERIFY_RESULT(GetOptionalArg<TxnSnapshotRestorationId>(args, 0));
  return PrintJsonResult(ListSnapshotRestorations(client, restoration_id));
}

const auto create_snapshot_schedule_args =
    "<snapshot_interval_in_minutes> <snapshot_retention_in_minutes> <keyspace>";
Status create_snapshot_schedule_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 3, 3));
  auto interval = MonoDelta::FromMinutes(VERIFY_RESULT(CheckedStold(args[0])));
  auto retention = MonoDelta::FromMinutes(VERIFY_RESULT(CheckedStold(args[1])));
  const auto tables = VERIFY_RESULT(
      ResolveTableNames(client, args.begin() + 2, args.end(), TailArgumentsProcessor(), true));
  // This is just a paranoid check, should never happen.
  if (tables.size() != 1 || !tables[0].has_namespace()) {
    return STATUS(InvalidArgument, "Expecting exactly one keyspace argument");
  }
  if (tables[0].namespace_type() != YQL_DATABASE_CQL &&
      tables[0].namespace_type() != YQL_DATABASE_PGSQL) {
    return STATUS(InvalidArgument, "Snapshot schedule can only be setup on YCQL or YSQL namespace");
  }
  return PrintJsonResult(client->CreateSnapshotSchedule(tables[0], interval, retention));
}

const auto list_snapshot_schedules_args = "[<schedule_id>]";
Status list_snapshot_schedules_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 0, 1));
  auto schedule_id = VERIFY_RESULT(GetOptionalArg<SnapshotScheduleId>(args, 0));
  return PrintJsonResult(client->ListSnapshotSchedules(schedule_id));
}

const auto delete_snapshot_schedule_args = "<schedule_id>";
Status delete_snapshot_schedule_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 1, 1));
  auto schedule_id = VERIFY_RESULT(SnapshotScheduleId::FromString(args[0]));
  return PrintJsonResult(client->DeleteSnapshotSchedule(schedule_id));
}

const auto restore_snapshot_schedule_args =
    Format("<schedule_id> (<timestamp> | $0 <interval>)", kMinus);
Status restore_snapshot_schedule_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 2, 3));
  auto schedule_id = VERIFY_RESULT(SnapshotScheduleId::FromString(args[0]));
  HybridTime restore_at;
  if (args.size() == 2) {
    restore_at = VERIFY_RESULT(HybridTime::ParseHybridTime(args[1]));
  } else {
    if (args[1] != kMinus) {
      return ClusterAdminCli::kInvalidArguments;
    }
    restore_at = VERIFY_RESULT(HybridTime::ParseHybridTime("-" + args[2]));
  }

  return PrintJsonResult(client->RestoreSnapshotSchedule(schedule_id, restore_at));
}

const auto clone_namespace_args =
    Format("<source_namespace> <target_namespace_name> [<timestamp> | $0 <interval>]", kMinus);
Status clone_namespace_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 2, 4));

  auto source_namespace = VERIFY_RESULT(ParseNamespaceName(args[0]));
  auto target_namespace_name = args[1];

  HybridTime restore_at;
  if (args.size() == 2) {
    auto now = VERIFY_RESULT(WallClock()->Now());
    restore_at = HybridTime::FromMicros(now.time_point);
  } else if (args.size() == 3) {
    restore_at = VERIFY_RESULT(HybridTime::ParseHybridTime(args[2]));
  } else {
    if (args[2] != kMinus) {
      return ClusterAdminCli::kInvalidArguments;
    }
    restore_at = VERIFY_RESULT(HybridTime::ParseHybridTime("-" + args[3]));
  }

  RETURN_NOT_OK(PrintJsonResult(
      client->CloneNamespace(source_namespace, target_namespace_name, restore_at)));
  return Status::OK();
}

const auto list_clones_args = "<source_namespace_id> [<seq_no>]";
Status list_clones_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 1, 2));

  auto source_namespace_id = args[0];
  std::optional<uint32_t> seq_no;
  if (args.size() >= 2) {
    seq_no = narrow_cast<uint32_t>(std::stoul(args[1]));
  }

  return PrintJsonResult(client->ListClones(source_namespace_id, seq_no));
}

const auto edit_snapshot_schedule_args =
    "<schedule_id> (interval <new_interval_in_minutes> | retention "
    "<new_retention_in_minutes>){1,2}";
Status edit_snapshot_schedule_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 3 && args.size() != 5) {
    return STATUS(InvalidArgument, Format("Expected 3 or 5 arguments, received $0", args.size()));
  }
  auto schedule_id = VERIFY_RESULT(SnapshotScheduleId::FromString(args[0]));
  std::optional<MonoDelta> new_interval;
  std::optional<MonoDelta> new_retention;
  for (size_t i = 1; i + 1 < args.size(); i += 2) {
    if (args[i] == "interval") {
      if (new_interval) {
        return STATUS(InvalidArgument, "Repeated interval");
      }
      new_interval = MonoDelta::FromMinutes(VERIFY_RESULT(CheckedStold(args[i + 1])));
    } else if (args[i] == "retention") {
      if (new_retention) {
        return STATUS(InvalidArgument, "Repeated retention");
      }
      new_retention = MonoDelta::FromMinutes(VERIFY_RESULT(CheckedStold(args[i + 1])));
    } else {
      return STATUS(
          InvalidArgument,
          Format("Expected either \"retention\" or \"interval\", got: $0", args[i]));
    }
  }
  return PrintJsonResult(client->EditSnapshotSchedule(schedule_id, new_interval, new_retention));
}

const auto create_keyspace_snapshot_args = "[ycql.]<database_name> [retention_duration_hours] "
    "(set a <= 0 value to retain the snapshot forever. If not specified "
    "then takes the default value controlled by gflag default_retention_hours) "
    "[skip_indexes] (if not specified then defaults to false)";
Status create_keyspace_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1 || args.size() > 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  std::optional<int32_t> retention_duration_hours;
  bool skip_indexes = false;
  for (size_t i = 1; i < args.size(); i++) {
    if (IsEqCaseInsensitive(args[i], "skip_indexes")) {
      skip_indexes = true;
    } else {
      retention_duration_hours = VERIFY_RESULT(CheckedStoi(args[i]));
    }
  }

  const TypedNamespaceName keyspace = VERIFY_RESULT(ParseNamespaceName(args[0]));
  SCHECK_NE(
      keyspace.db_type, YQL_DATABASE_PGSQL, InvalidArgument,
      Format("Wrong keyspace type: $0", YQLDatabase_Name(keyspace.db_type)));

  RETURN_NOT_OK_PREPEND(
      client->CreateNamespaceSnapshot(keyspace, retention_duration_hours, !skip_indexes),
      Format("Unable to create snapshot of keyspace: $0", keyspace.name));
  return Status::OK();
}

const auto create_database_snapshot_args = "[ysql.]<database_name> [retention_duration_hours] "
    "(set a <= 0 value to retain the snapshot forever. If not specified "
    "then takes the default value controlled by gflag default_retention_hours)";
Status create_database_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  std::optional<int32_t> retention_duration_hours;
  if (args.size() == 2) {
    retention_duration_hours = VERIFY_RESULT(CheckedStoi(args[1]));
  }

  const TypedNamespaceName database =
      VERIFY_RESULT(ParseNamespaceName(args[0], YQL_DATABASE_PGSQL));
  SCHECK_EQ(
      database.db_type, YQL_DATABASE_PGSQL, InvalidArgument,
      Format("Wrong database type: $0", YQLDatabase_Name(database.db_type)));

  RETURN_NOT_OK_PREPEND(
      client->CreateNamespaceSnapshot(database, retention_duration_hours),
      Format("Unable to create snapshot of database: $0", database.name));
  return Status::OK();
}

const auto restore_snapshot_args = Format("<snapshot_id> [<timestamp> | $0 <interval>]", kMinus);
Status restore_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1 || 3 < args.size()) {
    return ClusterAdminCli::kInvalidArguments;
  } else if (args.size() == 3 && args[1] != kMinus) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string snapshot_id = args[0];
  HybridTime timestamp;
  if (args.size() == 2) {
    timestamp = VERIFY_RESULT(HybridTime::ParseHybridTime(args[1]));
  } else if (args.size() == 3) {
    timestamp = VERIFY_RESULT(HybridTime::ParseHybridTime("-" + args[2]));
  }

  RETURN_NOT_OK_PREPEND(
      client->RestoreSnapshot(snapshot_id, timestamp),
      Format("Unable to restore snapshot $0", snapshot_id));
  return Status::OK();
}

const auto export_snapshot_args = "<snapshot_id> <file_name>";
Status export_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string snapshot_id = args[0];
  const string file_name = args[1];
  RETURN_NOT_OK_PREPEND(
      client->CreateSnapshotMetaFile(snapshot_id, file_name),
      Format("Unable to export snapshot $0 to file $1", snapshot_id, file_name));
  return Status::OK();
}

const auto import_snapshot_args = "<file_name> [<namespace> <table_name> [<table_name>]...]";
Status import_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return ImportSnapshot(client, args, false);
}

const auto import_snapshot_selective_args =
    "<file_name> [<namespace> <table_name> [<table_name>]...]";
Status import_snapshot_selective_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  return ImportSnapshot(client, args, true);
}

const auto delete_snapshot_args = "<snapshot_id>";
Status delete_snapshot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string snapshot_id = args[0];
  RETURN_NOT_OK_PREPEND(
      client->DeleteSnapshot(snapshot_id), Format("Unable to delete snapshot $0", snapshot_id));
  return Status::OK();
}

const auto abort_snapshot_restore_args = "[<restoration_id>]";
Status abort_snapshot_restore_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  auto restoration_id = VERIFY_RESULT(GetOptionalArg<TxnSnapshotRestorationId>(args, 0));
  RETURN_NOT_OK_PREPEND(
      client->AbortSnapshotRestore(restoration_id),
      Format("Unable to abort snapshot restore $0", restoration_id.ToString()));
  return Status::OK();
}

const auto list_replica_type_counts_args = "<table>";
Status list_replica_type_counts_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
  RETURN_NOT_OK_PREPEND(
      client->ListReplicaTypeCounts(table_name),
      "Unable to list live and read-only replica counts");
  return Status::OK();
}

const auto set_preferred_zones_args =
    "<cloud.region.zone>[:<priority>] [<cloud.region.zone>[:<priority>]]...";
Status set_preferred_zones_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(client->SetPreferredZones(args), "Unable to set preferred zones");
  return Status::OK();
}

const auto rotate_universe_key_args = "<key_path>";
Status rotate_universe_key_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(client->RotateUniverseKey(args[0]), "Unable to rotate universe key.");
  return Status::OK();
}

const auto disable_encryption_args = "";
Status disable_encryption_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->DisableEncryption(), "Unable to disable encryption.");
  return Status::OK();
}

const auto is_encryption_enabled_args = "";
Status is_encryption_enabled_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK_PREPEND(client->IsEncryptionEnabled(), "Unable to get encryption status.");
  return Status::OK();
}

const auto add_universe_key_to_all_masters_args = "<key_id> <key_path>";
Status add_universe_key_to_all_masters_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }
  string key_id = args[0];
  faststring contents;
  RETURN_NOT_OK(ReadFileToString(Env::Default(), args[1], &contents));
  string universe_key = contents.ToString();

  RETURN_NOT_OK_PREPEND(
      client->AddUniverseKeyToAllMasters(key_id, universe_key),
      "Unable to add universe key to all masters.");
  return Status::OK();
}

const auto all_masters_have_universe_key_in_memory_args = "<key_id>";
Status all_masters_have_universe_key_in_memory_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->AllMastersHaveUniverseKeyInMemory(args[0]),
      "Unable to check whether master has universe key in memory.");
  return Status::OK();
}

const auto rotate_universe_key_in_memory_args = "<key_id>";
Status rotate_universe_key_in_memory_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  string key_id = args[0];

  RETURN_NOT_OK_PREPEND(
      client->RotateUniverseKeyInMemory(key_id), "Unable rotate universe key in memory.");
  return Status::OK();
}

const auto disable_encryption_in_memory_args = "";
Status disable_encryption_in_memory_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(client->DisableEncryptionInMemory(), "Unable to disable encryption.");
  return Status::OK();
}

const auto write_universe_key_to_file_args = "<key_id> <file_name>";
Status write_universe_key_to_file_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }
  RETURN_NOT_OK_PREPEND(
      client->WriteUniverseKeyToFile(args[0], args[1]), "Unable to write key to file");
  return Status::OK();
}

const auto create_change_data_stream_args =
   "<namespace> [<checkpoint_type>] [<record_type>] [<consistent_snapshot_option>]";
Status create_change_data_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  std::string checkpoint_type = yb::ToString("EXPLICIT");
  cdc::CDCRecordType record_type_pb = cdc::CDCRecordType::CHANGE;
  std::string consistent_snapshot_option = "USE_SNAPSHOT";
  std::string uppercase_checkpoint_type;
  std::string uppercase_record_type;
  std::string uppercase_consistent_snapshot_option;


  if (args.size() > 1) {
    ToUpperCase(args[1], &uppercase_checkpoint_type);
    if (uppercase_checkpoint_type != yb::ToString("EXPLICIT") &&
        uppercase_checkpoint_type != yb::ToString("IMPLICIT")) {
      return ClusterAdminCli::kInvalidArguments;
    }
    checkpoint_type = uppercase_checkpoint_type;
  }

  if (args.size() > 2) {
    ToUpperCase(args[2], &uppercase_record_type);
    if (!cdc::CDCRecordType_Parse(uppercase_record_type, &record_type_pb)) {
      return ClusterAdminCli::kInvalidArguments;
    }
  }

  if (args.size() > 3) {
    ToUpperCase(args[3], &uppercase_consistent_snapshot_option);
    if (uppercase_consistent_snapshot_option != "USE_SNAPSHOT" &&
        uppercase_consistent_snapshot_option != "NOEXPORT_SNAPSHOT") {
      return ClusterAdminCli::kInvalidArguments;
    }
    consistent_snapshot_option = uppercase_consistent_snapshot_option;
  }

  const string namespace_name = args[0];
  const TypedNamespaceName database = VERIFY_RESULT(ParseNamespaceName(args[0]));

  RETURN_NOT_OK_PREPEND(
      client->CreateCDCSDKDBStream(
          database, checkpoint_type, record_type_pb, consistent_snapshot_option),
      Format("Unable to create CDC stream for database $0", namespace_name));
  return Status::OK();
}

const auto delete_cdc_stream_args = "<stream_id> [force_delete]";
Status delete_cdc_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string stream_id = args[0];
  bool force_delete = false;
  if (args.size() >= 2 && args[1] == "force_delete") {
    force_delete = true;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteCDCStream(stream_id, force_delete),
      Format("Unable to delete CDC stream id $0", stream_id));
  return Status::OK();
}

const auto delete_change_data_stream_args = "<db_stream_id>";
Status delete_change_data_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const std::string db_stream_id = args[0];
  RETURN_NOT_OK_PREPEND(
      client->DeleteCDCSDKDBStream(db_stream_id),
      Format("Unable to delete CDC database stream id $0", db_stream_id));
  return Status::OK();
}

const auto list_cdc_streams_args = "[<table_id>]";
Status list_cdc_streams_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0 && args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string table_id = (args.size() == 1 ? args[0] : "");
  RETURN_NOT_OK_PREPEND(
      client->ListCDCStreams(table_id),
      Format("Unable to list CDC streams for table $0", table_id));
  return Status::OK();
}

const auto list_change_data_streams_args = "[<namespace>]";
Status list_change_data_streams_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0 && args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string namespace_name = args.size() == 1 ? args[0] : "";
  string msg = (args.size() == 1)
                   ? Format("Unable to list CDC streams for namespace $0", namespace_name)
                   : "Unable to list CDC streams";

  RETURN_NOT_OK_PREPEND(client->ListCDCSDKStreams(namespace_name), msg);
  return Status::OK();
}

const auto get_change_data_stream_info_args = "<db_stream_id>";
Status get_change_data_stream_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0 && args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string db_stream_id = args.size() == 1 ? args[0] : "";
  RETURN_NOT_OK_PREPEND(
      client->GetCDCDBStreamInfo(db_stream_id),
      Format("Unable to list CDC stream info for database stream $0", db_stream_id));
  return Status::OK();
}

const auto ysql_backfill_change_data_stream_with_replication_slot_args =
    "<stream_id> <replication_slot_name>";
Status ysql_backfill_change_data_stream_with_replication_slot_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string stream_id = args[0];
  const string replication_slot_name = args[1];

  RETURN_NOT_OK_PREPEND(
      client->YsqlBackfillReplicationSlotNameToCDCSDKStream(stream_id, replication_slot_name),
      Format(
          "Unable to backfill CDC stream $0 with replication slot $1", stream_id,
          replication_slot_name));
  return Status::OK();
}

const auto disable_dynamic_table_addition_on_change_data_stream_args = "<stream_id>";
Status disable_dynamic_table_addition_on_change_data_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string stream_id = args[0];
  string msg = Format("Failed to disable dynamic table addition on CDC stream $0", stream_id);

  RETURN_NOT_OK_PREPEND(client->DisableDynamicTableAdditionOnCDCSDKStream(stream_id), msg);
  return Status::OK();
}

const auto remove_user_table_from_change_data_stream_args = "<stream_id> <table_id>";
Status remove_user_table_from_change_data_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string stream_id = args[0];
  const string table_id = args[1];
  string msg = Format("Failed to remove table $0 from CDC stream $1", table_id, stream_id);

  RETURN_NOT_OK_PREPEND(client->RemoveUserTableFromCDCSDKStream(stream_id, table_id), msg);
  return Status::OK();
}

const auto validate_and_sync_cdc_state_table_entries_on_change_data_stream_args = "<stream_id>";
Status validate_and_sync_cdc_state_table_entries_on_change_data_stream_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string stream_id = args[0];
  string msg =
      Format("Failed to validate and sync cdc state table entries for CDC stream $0", stream_id);

  RETURN_NOT_OK_PREPEND(client->ValidateAndSyncCDCStateEntriesForCDCSDKStream(stream_id), msg);
  return Status::OK();
}

const auto setup_universe_replication_args =
    "<producer_universe_uuid> <producer_master_addresses> "
    "<comma_separated_list_of_table_ids> [<comma_separated_list_of_producer_bootstrap_ids>] "
    "[transactional]";
Status setup_universe_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 3) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string replication_group_id = args[0];

  vector<string> producer_addresses;
  boost::split(producer_addresses, args[1], boost::is_any_of(","));

  vector<string> table_uuids;
  boost::split(table_uuids, args[2], boost::is_any_of(","));
  bool transactional = false;
  vector<string> producer_bootstrap_ids;
  if (args.size() > 3) {
    switch (args.size()) {
      case 4:
        if (IsEqCaseInsensitive(args[3], "transactional")) {
          transactional = true;
        } else {
          boost::split(producer_bootstrap_ids, args[3], boost::is_any_of(","));
        }
        break;
      case 5: {
        boost::split(producer_bootstrap_ids, args[3], boost::is_any_of(","));
        if (IsEqCaseInsensitive(args[3], "transactional")) {
          return ClusterAdminCli::kInvalidArguments;
        }
        transactional = true;
        break;
      }
      default:
        return ClusterAdminCli::kInvalidArguments;
    }
  }

  RETURN_NOT_OK_PREPEND(
      client->SetupUniverseReplication(
          replication_group_id, producer_addresses, table_uuids, producer_bootstrap_ids,
          transactional),
      Format("Unable to setup replication from universe $0", replication_group_id));
  return Status::OK();
}

const auto delete_universe_replication_args = "<producer_universe_uuid> [ignore-errors]";
Status delete_universe_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string replication_group_id = args[0];
  bool ignore_errors = false;
  if (args.size() >= 2 && args[1] == "ignore-errors") {
    ignore_errors = true;
  }
  RETURN_NOT_OK_PREPEND(
      client->DeleteUniverseReplication(replication_group_id, ignore_errors),
      Format("Unable to delete replication for universe $0", replication_group_id));
  return Status::OK();
}

const auto alter_universe_replication_args =
    "<producer_universe_uuid>"
    "(set_master_addresses [<comma_separated_list_of_producer_master_addresses>] | "
    "add_table [<comma_separated_list_of_table_ids>] "
    "[<comma_separated_list_of_producer_bootstrap_ids>] | "
    "remove_table [<comma_separated_list_of_table_ids>] [ignore-errors] | "
    "rename_id <new_producer_universe_id> | "
    "remove_namespace <source_namespace_id>)";
Status alter_universe_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 3 || args.size() > 4) {
    return ClusterAdminCli::kInvalidArguments;
  }
  if (args.size() == 4 && args[1] != "add_table" && args[1] != "remove_table") {
    return ClusterAdminCli::kInvalidArguments;
  }

  const string replication_group_id = args[0];
  vector<string> master_addresses;
  vector<string> add_tables;
  vector<string> remove_tables;
  vector<string> bootstrap_ids_to_add;
  string new_replication_group_id = "";
  bool remove_table_ignore_errors = false;
  NamespaceId source_namespace_to_remove;

  vector<string> newElem, *lst;
  if (args[1] == "set_master_addresses") {
    lst = &master_addresses;
  } else if (args[1] == "add_table") {
    lst = &add_tables;
  } else if (args[1] == "remove_table") {
    lst = &remove_tables;
    if (args.size() == 4 && args[3] == "ignore-errors") {
      remove_table_ignore_errors = true;
    }
  } else if (args[1] == "rename_id") {
    lst = nullptr;
    new_replication_group_id = args[2];
  } else if (args[1] == "remove_namespace") {
    lst = nullptr;
    source_namespace_to_remove = args[2];
  } else {
    return ClusterAdminCli::kInvalidArguments;
  }

  if (lst) {
    boost::split(newElem, args[2], boost::is_any_of(","));
    lst->insert(lst->end(), newElem.begin(), newElem.end());

    if (args[1] == "add_table" && args.size() == 4) {
      boost::split(bootstrap_ids_to_add, args[3], boost::is_any_of(","));
    }
  }

  RETURN_NOT_OK_PREPEND(
      client->AlterUniverseReplication(
          replication_group_id, master_addresses, add_tables, remove_tables, bootstrap_ids_to_add,
          new_replication_group_id, source_namespace_to_remove, remove_table_ignore_errors),
      Format("Unable to alter replication for universe $0", replication_group_id));

  return Status::OK();
}

const auto change_xcluster_role_args = "<STANDBY|ACTIVE>";
Status change_xcluster_role_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  std::cout << "Changed role successfully" << endl
            << endl
            << "NOTE: change_xcluster_role is no longer required and has been deprecated" << endl;
  return Status::OK();
}

const auto set_universe_replication_enabled_args = "<producer_universe_uuid> (0|1)";
Status set_universe_replication_enabled_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 2) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string replication_group_id = args[0];
  const bool is_enabled = VERIFY_RESULT(CheckedStoi(args[1])) != 0;
  RETURN_NOT_OK_PREPEND(
      client->SetUniverseReplicationEnabled(replication_group_id, is_enabled),
      Format(
          "Unable to $0 replication for universe $1", is_enabled ? "enable" : "disable",
          replication_group_id));
  return Status::OK();
}

const auto pause_producer_xcluster_streams_args =
    "(<comma_separated_list_of_stream_ids>|all) [resume]";
Status pause_producer_xcluster_streams_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 2 || args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  bool is_paused = true;
  vector<string> stream_ids;
  if (!boost::iequals(args[0], "all")) {
    boost::split(stream_ids, args[0], boost::is_any_of(","));
  }
  if (args.size() == 2) {
    if (!boost::iequals(args[args.size() - 1], "resume")) {
      return ClusterAdminCli::kInvalidArguments;
    }
    is_paused = false;
  }
  RETURN_NOT_OK_PREPEND(
      client->PauseResumeXClusterProducerStreams(stream_ids, is_paused),
      Format("Unable to $0 replication", is_paused ? "pause" : "resume"));
  return Status::OK();
}

const auto bootstrap_cdc_producer_args = "<comma_separated_list_of_table_ids>";
Status bootstrap_cdc_producer_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() < 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  vector<string> table_ids;
  boost::split(table_ids, args[0], boost::is_any_of(","));

  RETURN_NOT_OK_PREPEND(client->BootstrapProducer(table_ids), "Unable to bootstrap CDC producer");
  return Status::OK();
}

auto wait_for_replication_drain_args =
    Format("<comma_separated_list_of_stream_ids> [<timestamp> | $0 <interval>]", kMinus);
Status wait_for_replication_drain_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 1, 3));
  vector<string> stream_ids_str;
  boost::split(stream_ids_str, args[0], boost::is_any_of(","));
  vector<xrepl::StreamId> stream_ids;
  stream_ids.reserve(stream_ids_str.size());
  for (const auto& stream_id_str : stream_ids_str) {
    auto stream_id_result = xrepl::StreamId::FromString(stream_id_str);
    if (!stream_id_result.ok()) {
      return ClusterAdminCli::kInvalidArguments;
    }
    stream_ids.emplace_back(std::move(*stream_id_result));
  }

  string target_time;
  if (args.size() == 2) {
    target_time = args[1];
  } else if (args.size() == 3) {
    if (args[1] != kMinus) {
      return ClusterAdminCli::kInvalidArguments;
    }
    target_time = "-" + args[2];
  }

  return client->WaitForReplicationDrain(stream_ids, target_time);
}

const auto get_replication_status_args = "[<producer_universe_uuid>]";
Status get_replication_status_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0 && args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }
  const string replication_group_id = args.size() == 1 ? args[0] : "";
  RETURN_NOT_OK_PREPEND(
      client->GetReplicationInfo(replication_group_id), "Unable to get replication status");
  return Status::OK();
}

const auto get_xcluster_safe_time_args = "[include_lag_and_skew]";
Status get_xcluster_safe_time_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 0 && args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  bool include_lag_and_skew = false;
  if (args.size() > 0) {
    if (IsEqCaseInsensitive(args[0], "include_lag_and_skew")) {
      include_lag_and_skew = true;
    } else {
      return ClusterAdminCli::kInvalidArguments;
    }
  }

  return PrintJsonResult(client->GetXClusterSafeTime(include_lag_and_skew));
}

const auto create_xcluster_checkpoint_args = "<replication_group_id> <namespace_names>";
Status create_xcluster_checkpoint_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  std::vector<NamespaceName> namespace_names;
  boost::split(namespace_names, args[1], boost::is_any_of(","));
  std::vector<NamespaceName> namespace_ids;

  for (const auto& namespace_name : namespace_names) {
    const auto& namespace_info = VERIFY_RESULT_REF(
        client->GetNamespaceInfo(YQLDatabase::YQL_DATABASE_PGSQL, namespace_name));
    namespace_ids.push_back(namespace_info.id());
  }

  RETURN_NOT_OK(
      client->XClusterClient().CreateOutboundReplicationGroup(replication_group_id, namespace_ids));

  std::cout << "Waiting for checkpointing of database(s) to complete" << std::endl << std::endl;

  std::vector<NamespaceName> dbs_with_bootstrap;
  std::vector<NamespaceName> dbs_without_bootstrap;
  for (size_t i = 0; i < namespace_ids.size(); i++) {
    auto is_bootstrap_required =
        VERIFY_RESULT(client->IsXClusterBootstrapRequired(replication_group_id, namespace_ids[i]));
    std::cout << "Checkpointing of " << namespace_names[i] << " completed. Bootstrap is "
              << (is_bootstrap_required ? "" : "not ")
              << "required for setting up xCluster replication" << std::endl;
    if (is_bootstrap_required) {
      dbs_with_bootstrap.push_back(namespace_names[i]);
    } else {
      dbs_without_bootstrap.push_back(namespace_names[i]);
    }
  }
  std::cout << "Successfully checkpointed databases for xCluster replication group "
            << replication_group_id << std::endl
            << std::endl;

  if (!dbs_with_bootstrap.empty()) {
    std::cout << "Perform a distributed Backup of database(s) " << AsString(dbs_with_bootstrap)
              << " and Restore them on the target universe";
  }
  if (!dbs_without_bootstrap.empty()) {
    std::cout << "Create equivalent YSQL objects (schemas, tables, indexes, ...) for databases "
              << AsString(dbs_without_bootstrap) << " on the target universe";
  }

  std::cout << std::endl
            << "Once the above step(s) complete run `setup_xcluster_replication`" << std::endl;

  return Status::OK();
}

const auto is_xcluster_bootstrap_required_args = "<replication_group_id> <namespace_names>";
Status is_xcluster_bootstrap_required_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  std::vector<NamespaceName> namespace_names;
  boost::split(namespace_names, args[1], boost::is_any_of(","));

  std::cout << "Waiting for checkpointing of database(s) to complete" << std::endl << std::endl;

  for (size_t i = 0; i < namespace_names.size(); i++) {
    const auto& namespace_info = VERIFY_RESULT_REF(
        client->GetNamespaceInfo(YQLDatabase::YQL_DATABASE_PGSQL, namespace_names[i]));
    auto is_bootstrap_required = VERIFY_RESULT(
        client->IsXClusterBootstrapRequired(replication_group_id, namespace_info.id()));
    std::cout << "Checkpointing of " << namespace_names[i] << " completed. Bootstrap is "
              << (is_bootstrap_required ? "" : "not ")
              << "required for setting up xCluster replication" << std::endl;
  }

  return Status::OK();
}

const auto setup_xcluster_replication_args =
    "<replication_group_id> <target_master_addresses>";
Status setup_xcluster_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);

  RETURN_NOT_OK(client->XClusterClient().CreateXClusterReplicationFromCheckpoint(
      replication_group_id, args[1]));

  RETURN_NOT_OK(client->WaitForCreateXClusterReplication(replication_group_id, args[1]));

  std::cout << "xCluster Replication group " << replication_group_id << " setup successfully"
            << endl;

  return Status::OK();
}

const auto drop_xcluster_replication_args = "<replication_group_id> [<target_master_addresses>]";
Status drop_xcluster_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1 && args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);

  std::string target_master_addresses;
  if (args.size() == 2) {
    target_master_addresses = args[1];
  }

  RETURN_NOT_OK(client->XClusterClient().DeleteOutboundReplicationGroup(
      replication_group_id, target_master_addresses));

  std::cout << "Outbound xCluster Replication group " << replication_group_id
            << " deleted successfully" << endl;

  return Status::OK();
}

const auto add_namespace_to_xcluster_checkpoint_args = "<replication_group_id> <namespace_name>";
Status add_namespace_to_xcluster_checkpoint_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto& namespace_name = args[1];
  const auto& namespace_info =
      VERIFY_RESULT_REF(client->GetNamespaceInfo(YQLDatabase::YQL_DATABASE_PGSQL, namespace_name));
  const auto& namespace_id = namespace_info.id();

  RETURN_NOT_OK(client->XClusterClient().AddNamespaceToOutboundReplicationGroup(
      replication_group_id, namespace_id));

  std::cout << "Waiting for checkpointing of database to complete" << std::endl << std::endl;

  auto is_bootstrap_required =
      VERIFY_RESULT(client->IsXClusterBootstrapRequired(replication_group_id, namespace_id));

  std::cout << "Successfully checkpointed database " << namespace_name
            << " for xCluster replication group " << replication_group_id << std::endl
            << std::endl;

  std::cout << "Bootstrap is " << (is_bootstrap_required ? "" : "not ")
            << "required for adding database to xCluster replication" << std::endl;

  if (is_bootstrap_required) {
    std::cout
        << "Perform a distributed Backup of the database and Restore it on the target universe";
  } else {
    std::cout
        << "Create equivalent YSQL objects (schemas, tables, indexes, ...) for the database in "
           "the target universe";
  }

  std::cout << std::endl
            << "After completing the above step run `add_namespace_to_xcluster_replication`"
            << std::endl;

  return Status::OK();
}

const auto add_namespace_to_xcluster_replication_args =
    "<replication_group_id> <namespace_name> <target_master_addresses>";
Status add_namespace_to_xcluster_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto& namespace_info =
      VERIFY_RESULT_REF(client->GetNamespaceInfo(YQLDatabase::YQL_DATABASE_PGSQL, args[1]));

  const auto& target_master_addresses = args[2];

  RETURN_NOT_OK(client->XClusterClient().AddNamespaceToXClusterReplication(
      replication_group_id, target_master_addresses, namespace_info.id()));

  RETURN_NOT_OK(
      client->WaitForAlterXClusterReplication(replication_group_id, target_master_addresses));

  std::cout << "Successfully added " << namespace_info.name() << " to xCluster Replication group "
            << replication_group_id << endl;

  return Status::OK();
}

const auto remove_namespace_from_xcluster_replication_args =
    "<replication_group_id> <namespace_name> [<target_master_addresses>]";
Status remove_namespace_from_xcluster_replication_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2 && args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto& namespace_info =
      VERIFY_RESULT_REF(client->GetNamespaceInfo(YQLDatabase::YQL_DATABASE_PGSQL, args[1]));

  std::string target_master_addresses;
  if (args.size() == 3) {
    target_master_addresses = args[2];
  }

  RETURN_NOT_OK(client->XClusterClient().RemoveNamespaceFromOutboundReplicationGroup(
      replication_group_id, namespace_info.id(), target_master_addresses));

  std::cout << "Successfully removed " << namespace_info.name()
            << " from xCluster Replication group " << replication_group_id << endl;

  return Status::OK();
}

const auto repair_xcluster_outbound_replication_add_table_args =
    "<replication_group_id> <table_id> <stream_id>";
Status repair_xcluster_outbound_replication_add_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 3) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto& table_id = args[1];
  const auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(args[2]));

  RETURN_NOT_OK(client->XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
      replication_group_id, table_id, stream_id));

  std::cout << "Table " << table_id << " successfully added to outbound xCluster Replication group "
            << replication_group_id << endl;

  return Status::OK();
}

const auto repair_xcluster_outbound_replication_remove_table_args =
    "<replication_group_id> <table_id>";
Status repair_xcluster_outbound_replication_remove_table_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 2) {
    return ClusterAdminCli::kInvalidArguments;
  }

  auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto& table_id = args[1];

  RETURN_NOT_OK(client->XClusterClient().RepairOutboundXClusterReplicationGroupRemoveTable(
      replication_group_id, table_id));

  std::cout << "Table " << table_id
            << " successfully removed from outbound xCluster Replication group "
            << replication_group_id << endl;

  return Status::OK();
}

const auto list_xcluster_outbound_replication_groups_args = "[namespace_id]";
Status list_xcluster_outbound_replication_groups_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  NamespaceId namespace_id;
  if (args.size() > 0) {
    namespace_id = args[0];
  }

  auto group_ids =
      VERIFY_RESULT(client->XClusterClient().GetXClusterOutboundReplicationGroups(namespace_id));

  std::cout << group_ids.size() << " Outbound Replication Groups found"
            << (namespace_id.empty() ? "" : Format(" for namespace $0", namespace_id)) << ": "
            << std::endl
            << yb::AsString(group_ids) << std::endl;

  return Status::OK();
}

const auto get_xcluster_outbound_replication_group_info_args = "<replication_group_id>";
Status get_xcluster_outbound_replication_group_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto group_info = VERIFY_RESULT(
      client->XClusterClient().GetXClusterOutboundReplicationGroupInfo(replication_group_id));

  const auto& namespace_map = VERIFY_RESULT_REF(client->GetNamespaceMap());

  std::cout << "Outbound Replication Group: " << replication_group_id << std::endl;

  for (const auto& [namespace_id, table_info] : group_info) {
    std::cout << std::endl << "Namespace ID: " << namespace_id << std::endl;
    auto* namespace_info = FindOrNull(namespace_map, namespace_id);
    std::cout << "Namespace name: " << (namespace_info ? namespace_info->id.name() : "<N/A>")
              << std::endl
              << "Table Id\t\tStream Id" << std::endl;
    for (const auto& [table_id, stream_id] : table_info) {
      std::cout << table_id << "\t\t" << stream_id << std::endl;
    }
  }

  return Status::OK();
}

const auto list_universe_replications_args = "[namespace_id]";
Status list_universe_replications_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() > 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  NamespaceId namespace_id;
  if (args.size() > 0) {
    namespace_id = args[0];
  }

  auto group_ids = VERIFY_RESULT(client->XClusterClient().GetUniverseReplications(namespace_id));

  std::cout << group_ids.size() << " Universe Replication Groups found"
            << (namespace_id.empty() ? "" : Format(" for namespace $0", namespace_id)) << ": "
            << std::endl
            << yb::AsString(group_ids) << std::endl;

  return Status::OK();
}

const auto get_universe_replication_info_args = "<replication_group_id>";
Status get_universe_replication_info_action(
    const ClusterAdminCli::CLIArguments& args, ClusterAdminClient* client) {
  if (args.size() != 1) {
    return ClusterAdminCli::kInvalidArguments;
  }

  const auto replication_group_id = xcluster::ReplicationGroupId(args[0]);
  const auto group_info =
      VERIFY_RESULT(client->XClusterClient().GetUniverseReplicationInfo(replication_group_id));

  const auto& namespace_map = VERIFY_RESULT_REF(client->GetNamespaceMap());

  std::cout << "Replication Group Id: " << replication_group_id << std::endl;
  std::cout << "Source master addresses: " << group_info.source_master_addrs << std::endl;
  std::cout << "Type: " << xcluster::ShortReplicationType(group_info.replication_type) << std::endl;

  if (group_info.replication_type == XClusterReplicationType::XCLUSTER_YSQL_DB_SCOPED) {
    std::cout << std::endl
              << "DB Scoped info(s):" << std::endl
              << "Namespace name\t\tTarget Namespace ID\t\tSource Namespace ID" << std::endl;
    for (const auto& [target_namespace_id, source_namespace_id] :
         group_info.db_scope_namespace_id_map) {
      auto* namespace_info = FindOrNull(namespace_map, target_namespace_id);
      std::cout << (namespace_info ? namespace_info->id.name() : "<N/A>") << "\t\t"
                << target_namespace_id << "\t\t" << source_namespace_id << std::endl;
    }
  }

  std::cout << std::endl
            << "Tables:" << std::endl
            << "Target Table ID\t\tSource Table ID\t\tStreamId" << std::endl;
  for (const auto& tables : group_info.table_infos) {
    std::cout << tables.target_table_id << "\t\t" << tables.source_table_id << "\t\t"
              << tables.stream_id << std::endl;
  }

  return Status::OK();
}

}  // namespace

void ClusterAdminCli::RegisterCommandHandlers() {
  DCHECK_ONLY_NOTNULL(client_);

  REGISTER_COMMAND(change_config);
  REGISTER_COMMAND(list_tablet_servers);
  REGISTER_COMMAND(backfill_indexes_for_table);
  REGISTER_COMMAND(list_tables);
  REGISTER_COMMAND(list_tables_with_db_types);
  REGISTER_COMMAND(list_tablets);
  REGISTER_COMMAND(modify_table_placement_info);
  REGISTER_COMMAND(modify_placement_info);
  REGISTER_COMMAND(clear_placement_info);
  REGISTER_COMMAND(add_read_replica_placement_info);
  REGISTER_COMMAND(modify_read_replica_placement_info);
  REGISTER_COMMAND(delete_read_replica_placement_info);
  REGISTER_COMMAND(list_namespaces);
  REGISTER_COMMAND(delete_namespace);
  REGISTER_COMMAND(delete_namespace_by_id);
  REGISTER_COMMAND(delete_table);
  REGISTER_COMMAND(delete_table_by_id);
  REGISTER_COMMAND(delete_index);
  REGISTER_COMMAND(delete_index_by_id);
  REGISTER_COMMAND(flush_table);
  REGISTER_COMMAND(flush_table_by_id);
  REGISTER_COMMAND(flush_sys_catalog);
  REGISTER_COMMAND(compact_sys_catalog);
  REGISTER_COMMAND(compact_table);
  REGISTER_COMMAND(compact_table_by_id);
  REGISTER_COMMAND(compaction_status);
  REGISTER_COMMAND(list_all_tablet_servers);
  REGISTER_COMMAND(list_all_masters);
  REGISTER_COMMAND(change_master_config);
  REGISTER_COMMAND(dump_masters_state);
  REGISTER_COMMAND(list_tablet_server_log_locations);
  REGISTER_COMMAND(list_tablets_for_tablet_server);
  REGISTER_COMMAND(set_load_balancer_enabled);
  REGISTER_COMMAND(get_load_balancer_state);
  REGISTER_COMMAND(get_load_move_completion);
  REGISTER_COMMAND(get_leader_blacklist_completion);
  REGISTER_COMMAND(get_is_load_balancer_idle);
  REGISTER_COMMAND(list_leader_counts);
  REGISTER_COMMAND(setup_redis_table);
  REGISTER_COMMAND(drop_redis_table);
  REGISTER_COMMAND(get_universe_config);
  REGISTER_COMMAND(get_xcluster_info);
  REGISTER_COMMAND(change_blacklist);
  REGISTER_COMMAND(change_leader_blacklist);
  REGISTER_COMMAND(master_leader_stepdown);
  REGISTER_COMMAND(leader_stepdown);
  REGISTER_COMMAND(split_tablet);
  REGISTER_COMMAND(disable_tablet_splitting);
  REGISTER_COMMAND(is_tablet_splitting_complete);
  REGISTER_COMMAND(create_transaction_table);
  REGISTER_COMMAND(add_transaction_tablet);
  REGISTER_COMMAND(ysql_catalog_version);
  REGISTER_COMMAND(ddl_log);
  REGISTER_COMMAND(upgrade_ysql);
  REGISTER_COMMAND(set_wal_retention_secs);
  REGISTER_COMMAND(get_wal_retention_secs);
  REGISTER_COMMAND(get_auto_flags_config);
  REGISTER_COMMAND(promote_auto_flags);
  REGISTER_COMMAND(rollback_auto_flags);
  REGISTER_COMMAND(promote_single_auto_flag);
  REGISTER_COMMAND(demote_single_auto_flag);
  REGISTER_COMMAND(list_snapshots);
  REGISTER_COMMAND(create_snapshot);
  REGISTER_COMMAND(list_snapshot_restorations);
  REGISTER_COMMAND(create_snapshot_schedule);
  REGISTER_COMMAND(list_snapshot_schedules);
  REGISTER_COMMAND(delete_snapshot_schedule);
  REGISTER_COMMAND(restore_snapshot_schedule);
  REGISTER_COMMAND(clone_namespace);
  REGISTER_COMMAND(list_clones);
  REGISTER_COMMAND(edit_snapshot_schedule);
  REGISTER_COMMAND(create_keyspace_snapshot);
  REGISTER_COMMAND(create_database_snapshot);
  REGISTER_COMMAND(restore_snapshot);
  REGISTER_COMMAND(export_snapshot);
  REGISTER_COMMAND(import_snapshot);
  REGISTER_COMMAND(import_snapshot_selective);
  REGISTER_COMMAND(delete_snapshot);
  REGISTER_COMMAND(abort_snapshot_restore);
  REGISTER_COMMAND(list_replica_type_counts);
  REGISTER_COMMAND(set_preferred_zones);
  REGISTER_COMMAND(rotate_universe_key);
  REGISTER_COMMAND(disable_encryption);
  REGISTER_COMMAND(is_encryption_enabled);
  REGISTER_COMMAND(add_universe_key_to_all_masters);
  REGISTER_COMMAND(all_masters_have_universe_key_in_memory);
  REGISTER_COMMAND(rotate_universe_key_in_memory);
  REGISTER_COMMAND(disable_encryption_in_memory);
  REGISTER_COMMAND(write_universe_key_to_file);
  // CDCSDK commands
  REGISTER_COMMAND(create_change_data_stream);
  REGISTER_COMMAND(delete_change_data_stream);
  REGISTER_COMMAND(list_change_data_streams);
  REGISTER_COMMAND(get_change_data_stream_info);
  REGISTER_COMMAND(ysql_backfill_change_data_stream_with_replication_slot);
  REGISTER_COMMAND(disable_dynamic_table_addition_on_change_data_stream);
  REGISTER_COMMAND(remove_user_table_from_change_data_stream);
  REGISTER_COMMAND(validate_and_sync_cdc_state_table_entries_on_change_data_stream);
  // xCluster Source commands
  REGISTER_COMMAND(bootstrap_cdc_producer);
  REGISTER_COMMAND(list_cdc_streams);
  REGISTER_COMMAND(delete_cdc_stream);
  REGISTER_COMMAND(pause_producer_xcluster_streams);
  REGISTER_COMMAND(wait_for_replication_drain);
  // xCluster Target commands
  REGISTER_COMMAND(setup_universe_replication);
  REGISTER_COMMAND(delete_universe_replication);
  REGISTER_COMMAND(alter_universe_replication);
  REGISTER_COMMAND(set_universe_replication_enabled);
  REGISTER_COMMAND(get_replication_status);
  REGISTER_COMMAND(get_xcluster_safe_time);
  REGISTER_COMMAND(list_universe_replications);
  REGISTER_COMMAND(get_universe_replication_info);
  // xCluster common commands
  REGISTER_COMMAND(change_xcluster_role);

  // xCluster V2 commands
  REGISTER_COMMAND(create_xcluster_checkpoint);
  REGISTER_COMMAND(is_xcluster_bootstrap_required);
  REGISTER_COMMAND(setup_xcluster_replication);
  REGISTER_COMMAND(drop_xcluster_replication);
  REGISTER_COMMAND(add_namespace_to_xcluster_checkpoint);
  REGISTER_COMMAND(add_namespace_to_xcluster_replication);
  REGISTER_COMMAND(remove_namespace_from_xcluster_replication);
  REGISTER_COMMAND(repair_xcluster_outbound_replication_add_table);
  REGISTER_COMMAND(repair_xcluster_outbound_replication_remove_table);
  REGISTER_COMMAND(list_xcluster_outbound_replication_groups);
  REGISTER_COMMAND(get_xcluster_outbound_replication_group_info);
}

Result<std::vector<client::YBTableName>> ResolveTableNames(
    ClusterAdminClient* client, CLIArgumentsIterator i, const CLIArgumentsIterator& end,
    const TailArgumentsProcessor& tail_processor, bool allow_namespace_only) {
  TableNameResolver::Values tables;
  auto resolver = VERIFY_RESULT(client->BuildTableNameResolver(&tables));
  auto tail = i;
  Status resolver_failure = Status::OK();
  // Greedy algorithm of taking as much tables as possible.
  for (; i != end; ++i) {
    auto result = resolver.Feed(*i);
    if (!result.ok()) {
      // If tail arguments were not processed suppose it is bad table
      // and return its parsing error instead.
      resolver_failure = std::move(result.status());
      break;
    }
    if (*result) {
      tail = std::next(i);
    }
  }

  if (tables.empty() && allow_namespace_only) {
    const auto* last_namespace = resolver.last_namespace();
    if (last_namespace && !last_namespace->name().empty()) {
      client::YBTableName table_name;
      table_name.GetFromNamespaceIdentifierPB(*last_namespace);
      tables.push_back(std::move(table_name));
      ++tail;
    }
  }

  if (tables.empty()) {
    return PrioritizedError(
        std::move(resolver_failure), STATUS(InvalidArgument, "Empty list of tables"));
  }

  if (tail != end && tail_processor) {
    auto status = tail_processor(tail, end);
    if (!status.ok()) {
      return PrioritizedError(std::move(resolver_failure), std::move(status));
    }
  }
  return tables;
}

Result<client::YBTableName> ResolveSingleTableName(
    ClusterAdminClient* client, CLIArgumentsIterator i, const CLIArgumentsIterator& end,
    TailArgumentsProcessor tail_processor) {
  auto tables = VERIFY_RESULT(ResolveTableNames(client, i, end, tail_processor));
  if (tables.size() != 1) {
    return STATUS_FORMAT(InvalidArgument, "Single table expected, $0 found", tables.size());
  }
  return std::move(tables.front());
}

Status CheckArgumentsCount(size_t count, size_t min, size_t max) {
  if (count < min) {
    return STATUS_FORMAT(
        InvalidArgument, "Too few arguments $0, should be in range [$1, $2]", count, min, max);
  }

  if (count > max) {
    return STATUS_FORMAT(
        InvalidArgument, "Too many arguments $0, should be in range [$1, $2]", count, min, max);
  }

  return Status::OK();
}

}  // namespace tools
}  // namespace yb

int main(int argc, char** argv) {
  yb::Status s = yb::tools::ClusterAdminCli().Run(argc, argv);
  if (s.ok()) {
    return 0;
  }

  if (s.IsInvalidArgument()) {
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
  }

  return 1;
}
