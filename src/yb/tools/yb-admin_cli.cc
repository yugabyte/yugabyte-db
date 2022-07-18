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

#include <boost/lexical_cast.hpp>

#include "yb/common/json_util.h"

#include "yb/master/master_defaults.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_case.h"

DEFINE_string(master_addresses, "localhost:7100",
              "Comma-separated list of YB Master server addresses");
DEFINE_string(init_master_addrs, "",
              "host:port of any yb-master in a cluster");
DEFINE_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");
DEFINE_bool(exclude_dead, false, "Exclude dead tservers from output");


using std::cerr;
using std::endl;
using std::ostringstream;
using std::make_pair;
using std::next;
using std::pair;
using std::string;

using yb::client::YBTableName;
using strings::Substitute;

using namespace std::placeholders;

namespace yb {
namespace tools {

const Status ClusterAdminCli::kInvalidArguments = STATUS(
                                  InvalidArgument, "Invalid arguments for operation");

namespace {

constexpr auto kBlacklistAdd = "ADD";
constexpr auto kBlacklistRemove = "REMOVE";
constexpr int32 kDefaultRpcPort = 9100;

Status GetUniverseConfig(ClusterAdminClientClass* client,
                                 const ClusterAdminCli::CLIArguments&) {
  RETURN_NOT_OK_PREPEND(client->GetUniverseConfig(), "Unable to get universe config");
  return Status::OK();
}

Status ChangeBlacklist(ClusterAdminClientClass* client,
                               const ClusterAdminCli::CLIArguments& args, bool blacklist_leader,
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

  RETURN_NOT_OK_PREPEND(client->ChangeBlacklist(hostports, change_type == kBlacklistAdd,
        blacklist_leader), errStr);
  return Status::OK();
}

Status MasterLeaderStepDown(
    ClusterAdminClientClass* client,
    const ClusterAdminCli::CLIArguments& args) {
  const auto leader_uuid = VERIFY_RESULT(client->GetMasterLeaderUuid());
  return client->MasterLeaderStepDown(
      leader_uuid, args.size() > 0 ? args[0] : std::string());
}

Status LeaderStepDown(
    ClusterAdminClientClass* client,
    const ClusterAdminCli::CLIArguments& args) {
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
Result<std::pair<int, EnumBitSet<Enum>>> GetValueAndFlags(
    const CLIArgumentsIterator& begin,
    const CLIArgumentsIterator& end,
    const AllEnumItemsIterable<Enum>& flags_list) {
  std::pair<int, EnumBitSet<Enum>> result;
  bool seen_value = false;
  for (auto iter = begin; iter != end; iter = ++iter) {
    bool found_flag = false;
    for (auto flag : flags_list) {
      if (IsEqCaseInsensitive(*iter, ToString(flag))) {
        if (result.second.Test(flag)) {
          return STATUS_FORMAT(InvalidArgument, "Duplicate flag: $0", flag);
        }
        result.second.Set(flag);
        found_flag = true;
        break;
      }
    }
    if (found_flag) {
      continue;
    }

    if (seen_value) {
      return STATUS_FORMAT(InvalidArgument, "Multiple values: $0 and $1", result.first, *iter);
    }

    result.first = VERIFY_RESULT(CheckedStoi(*iter));
    seen_value = true;
  }

  return result;
}

YB_DEFINE_ENUM(AddIndexes, (ADD_INDEXES));

Result<pair<int, bool>> GetTimeoutAndAddIndexesFlag(
    CLIArgumentsIterator begin,
    const CLIArgumentsIterator& end) {
  auto temp_pair = VERIFY_RESULT(GetValueAndFlags(begin, end, AddIndexesList()));
  return std::make_pair(temp_pair.first, temp_pair.second.Test(AddIndexes::ADD_INDEXES));
}

YB_DEFINE_ENUM(ListTabletsFlags, (JSON)(INCLUDE_FOLLOWERS));

} // namespace

Status ClusterAdminCli::RunCommand(
    const Command& command, const CLIArguments& command_args, const std::string& program_name) {
  auto s = command.action_(command_args);
  if (!s.ok()) {
    if (s.IsRemoteError() && s.ToString().find("rpc error 2")) {
      cerr << "The cluster doesn't support " << command.name_ << ": " << s << std::endl;
    } else {
      cerr << "Error running " << command.name_ << ": " << s << endl;
      if (s.IsInvalidArgument()) {
        cerr << Format("Usage: $0 $1 $2", program_name, command.name_, command.usage_arguments_)
             << endl;
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

  std::unique_ptr<ClusterAdminClientClass> client;
  const string addrs = FLAGS_master_addresses;
  if (!FLAGS_init_master_addrs.empty()) {
    std::vector<HostPort> init_master_addrs;
    RETURN_NOT_OK(HostPort::ParseStrings(
        FLAGS_init_master_addrs,
        master::kMasterDefaultPort,
        &init_master_addrs));
    client.reset(new ClusterAdminClientClass(
        init_master_addrs[0],
        MonoDelta::FromMilliseconds(FLAGS_timeout_ms)));
  } else {
    client.reset(new ClusterAdminClientClass(
        addrs,
        MonoDelta::FromMilliseconds(FLAGS_timeout_ms)));
  }

  RegisterCommandHandlers(client.get());
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
  Status s = client->Init();

  if (PREDICT_FALSE(!s.ok())) {
    cerr << s.CloneAndPrepend("Unable to establish connection to leader master at [" + addrs + "]."
                              " Please verify the addresses.\n\n").ToString() << endl;
    return STATUS(RuntimeError, "Error connecting to cluster");
  }

  CLIArguments command_args(args.begin() + 2, args.end());
  auto& command = commands_[cmd->second];
  return RunCommand(command, command_args, args[0]);
}

void ClusterAdminCli::Register(string&& cmd_name, string&& cmd_args, Action&& action) {
  command_indexes_[cmd_name] = commands_.size();
  commands_.push_back({std::move(cmd_name), std::move(cmd_args), std::move(action)});
}

void ClusterAdminCli::RegisterJson(string&& cmd_name, string&& cmd_args, JsonAction&& action) {
  Register(std::move(cmd_name), std::move(cmd_args), [action](const CLIArguments& args) -> Status {
    auto result = VERIFY_RESULT(action(args));
    std::cout << common::PrettyWriteRapidJsonToString(result) << std::endl;
    return Status::OK();
  });
}

void ClusterAdminCli::SetUsage(const string& prog_name) {
  ostringstream str;

  str << prog_name << " [-master_addresses server1:port,server2:port,server3:port,...] "
      << " [-timeout_ms <millisec>] [-certs_dir_name <dir_name>] <operation>" << endl
      << "<operation> must be one of:" << endl;

  for (size_t i = 0; i < commands_.size(); ++i) {
    str << ' ' << i + 1 << ". " << commands_[i].name_ << commands_[i].usage_arguments_ << endl;
  }

  str << endl;
  str << "<namespace>:" << endl;
  str << "  [(ycql|ysql).]<namespace_name>" << endl;
  str << "<table>:" << endl;
  str << "  <namespace> <table_name> | tableid.<table_id>" << endl;
  str << "<index>:" << endl;
  str << "  <namespace> <index_name> | tableid.<index_id>" << endl;

  google::SetUsageMessage(str.str());
}

Result<rapidjson::Document> DdlLog(
    ClusterAdminClientClass* client, const ClusterAdminCli::CLIArguments& args) {
  RETURN_NOT_OK(CheckArgumentsCount(args.size(), 0, 0));
  return client->DdlLog();
}

void ClusterAdminCli::RegisterCommandHandlers(ClusterAdminClientClass* client) {
  DCHECK_ONLY_NOTNULL(client);

  Register(
      "change_config",
      " <tablet_id> <ADD_SERVER|REMOVE_SERVER> <peer_uuid> [PRE_VOTER|PRE_OBSERVER]",
      [client](const CLIArguments& args) -> Status {
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
        RETURN_NOT_OK_PREPEND(client->ChangeConfig(tablet_id, change_type, peer_uuid, member_type),
                              "Unable to change config");
        return Status::OK();
      });

  Register(
      "list_tablet_servers", " <tablet_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string tablet_id = args[0];
        RETURN_NOT_OK_PREPEND(client->ListPerTabletTabletServers(tablet_id),
                              Substitute("Unable to list tablet servers of tablet $0", tablet_id));
        return Status::OK();
      });

  Register(
      "backfill_indexes_for_table", " <table>",
      [client](const CLIArguments& args) -> Status {
        const auto table_name = VERIFY_RESULT(
            ResolveSingleTableName(client, args.begin(), args.end()));
        RETURN_NOT_OK_PREPEND(client->LaunchBackfillIndexForTable(table_name),
                              yb::Format("Unable to launch backfill for indexes in $0",
                                         table_name));
        return Status::OK();
      });

  static const auto kIncludeDBType = "include_db_type";
  static const auto kIncludeTableId = "include_table_id";
  static const auto kIncludeTableType = "include_table_type";

  Register(
      "list_tables", Format(" [$0] [$1] [$2]", kIncludeDBType, kIncludeTableId, kIncludeTableType),
      [client](const CLIArguments& args) -> Status {
        bool include_db_type = false;
        bool include_table_id = false;
        bool include_table_type = false;
        const std::array<std::pair<const char*, bool*>, 3> flags{
            std::make_pair(kIncludeDBType, &include_db_type),
            std::make_pair(kIncludeTableId, &include_table_id),
            std::make_pair(kIncludeTableType, &include_table_type)
        };
        for (const auto& arg : args) {
          for (const auto& flag : flags) {
            if (flag.first == arg) {
              *flag.second = true;
            }
          }
        }
        RETURN_NOT_OK_PREPEND(client->ListTables(include_db_type,
                                                 include_table_id,
                                                 include_table_type),
                              "Unable to list tables");
        return Status::OK();
      });

  // Deprecated list_tables commands with arguments should be used instead.
  Register(
      "list_tables_with_db_types", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(
            client->ListTables(true /* include_db_type */,
                               false /* include_table_id*/,
                               false /* include_table_type*/),
            "Unable to list tables");
        return Status::OK();
      });

  Register(
      "list_tablets",
      " <table> [max_tablets] (default 10, set 0 for max) [JSON] [include_followers]",
      [client](const CLIArguments& args) -> Status {
        std::pair<int, EnumBitSet<ListTabletsFlags>> arguments;
        const auto table_name  = VERIFY_RESULT(ResolveSingleTableName(
            client, args.begin(), args.end(),
            [&arguments](auto i, const auto& end) -> Status {
              arguments = VERIFY_RESULT(GetValueAndFlags(i, end, ListTabletsFlagsList()));
              return Status::OK();
            }));
        RETURN_NOT_OK_PREPEND(
            client->ListTablets(
                table_name, arguments.first, arguments.second.Test(ListTabletsFlags::JSON),
                arguments.second.Test(ListTabletsFlags::INCLUDE_FOLLOWERS)),
            Format("Unable to list tablets of table $0", table_name));
        return Status::OK();
      });

  static const auto kTableName = "<(<keyspace> <table_name>)|tableid.<table_id>>";
  static const auto kPlacementInfo = "placement_info";
  static const auto kReplicationFactor = "replication_factor";
  static const auto kPlacementUuid = "placement_uuid";

  Register(
      "modify_table_placement_info",
        Format(" [$0] [$1] [$2] [$3]", kTableName, kPlacementInfo, kReplicationFactor,
          kPlacementUuid),
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3 || args.size() > 5) {
          return ClusterAdminCli::kInvalidArguments;
        }
        std::string placement_info;
        int rf = -1;
        std::string placement_uuid;
        const auto table_name  = VERIFY_RESULT(ResolveSingleTableName(
            client, args.begin(), args.end(),
            [&placement_info, &rf, &placement_uuid](
        auto i, const auto& end) -> Status {
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
        }
        ));
        RETURN_NOT_OK_PREPEND(
            client->ModifyTablePlacementInfo(table_name, placement_info, rf, placement_uuid),
            Substitute("Unable to modify placement info for table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "modify_placement_info", " <placement_info> <replication_factor> [placement_uuid]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 2 && args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        int rf = boost::lexical_cast<int>(args[1]);
        string placement_uuid = args.size() == 3 ? args[2] : "";
        RETURN_NOT_OK_PREPEND(client->ModifyPlacementInfo(args[0], rf, placement_uuid),
                              Substitute("Unable to modify placement info."));
        return Status::OK();
      });

  Register(
      "clear_placement_info", "",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 0) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->ClearPlacementInfo(),
                              Substitute("Unable to clear placement info."));
        return Status::OK();
      });

  Register(
      "add_read_replica_placement_info", " <placement_info> <replication_factor> [placement_uuid]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 2 && args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        int rf = boost::lexical_cast<int>(args[1]);
        string placement_uuid = args.size() == 3 ? args[2] : "";
        RETURN_NOT_OK_PREPEND(client->AddReadReplicaPlacementInfo(args[0], rf, placement_uuid),
                              Substitute("Unable to add read replica placement info."));
        return Status::OK();
      });

  Register(
      "modify_read_replica_placement_info",
      " <placement_info> <replication_factor> [placement_uuid]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 2 && args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        int rf = boost::lexical_cast<int>(args[1]);
        string placement_uuid = args.size() == 3 ? args[2] : "";
        RETURN_NOT_OK_PREPEND(client->ModifyReadReplicaPlacementInfo(placement_uuid, args[0], rf),
                              Substitute("Unable to modify read replica placement info."));
        return Status::OK();
      });

  Register(
      "delete_read_replica_placement_info", "",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 0) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->DeleteReadReplicaPlacementInfo(),
                              Substitute("Unable to delete read replica placement info."));
        return Status::OK();
      });

  Register(
      "delete_namespace", " <namespace>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        auto namespace_name = VERIFY_RESULT(ParseNamespaceName(args[0]));
        RETURN_NOT_OK_PREPEND(client->DeleteNamespace(namespace_name),
                              Substitute("Unable to delete namespace $0", namespace_name.name));
        return Status::OK();
      });

  Register(
      "delete_namespace_by_id", " <namespace_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->DeleteNamespaceById(args[0]),
                              Substitute("Unable to delete namespace $0", args[0]));
        return Status::OK();
      });

  Register(
      "delete_table", " <table>",
      [client](const CLIArguments& args) -> Status {
        const auto table_name = VERIFY_RESULT(
            ResolveSingleTableName(client, args.begin(), args.end()));
        RETURN_NOT_OK_PREPEND(client->DeleteTable(table_name),
                              Substitute("Unable to delete table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "delete_table_by_id", " <table_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->DeleteTableById(args[0]),
                              Substitute("Unable to delete table $0", args[0]));
        return Status::OK();
      });

  Register(
      "delete_index", " <index>",
      [client](const CLIArguments& args) -> Status {
        const auto table_name = VERIFY_RESULT(
            ResolveSingleTableName(client, args.begin(), args.end()));
        RETURN_NOT_OK_PREPEND(client->DeleteIndex(table_name),
                              Substitute("Unable to delete index $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "delete_index_by_id", " <index_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->DeleteIndexById(args[0]),
                              Substitute("Unable to delete index $0", args[0]));
        return Status::OK();
      });

  Register(
      "flush_table",
      " <table> [timeout_in_seconds] (default 20)"
      " [ADD_INDEXES] (default false)",
      [client](const CLIArguments& args) -> Status {
        bool add_indexes = false;
        int timeout_secs = 20;
        const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
            client, args.begin(), args.end(),
            [&add_indexes, &timeout_secs](auto i, const auto& end) -> Status {
              std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(
                  GetTimeoutAndAddIndexesFlag(i, end));
              return Status::OK();
            }));
        RETURN_NOT_OK_PREPEND(client->FlushTables({table_name},
                                                  add_indexes,
                                                  timeout_secs,
                                                  false /* is_compaction */),
                              Substitute("Unable to flush table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "flush_table_by_id", " <table_id> [timeout_in_seconds] (default 20)"
      " [ADD_INDEXES] (default false)",
      [client](const CLIArguments& args) -> Status {
        bool add_indexes = false;
        int timeout_secs = 20;
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(GetTimeoutAndAddIndexesFlag(
          args.begin() + 1, args.end()));
        RETURN_NOT_OK_PREPEND(client->FlushTablesById({args[0]},
                                                      add_indexes,
                                                      timeout_secs,
                                                      false /* is_compaction */),
                              Substitute("Unable to flush table $0", args[0]));
        return Status::OK();
      });

  Register(
      "flush_sys_catalog", "",
      [client](const CLIArguments& args) -> Status {
        RETURN_NOT_OK_PREPEND(client->FlushSysCatalog(), "Unable to flush table sys_catalog");
        return Status::OK();
      });

  Register(
      "compact_sys_catalog", "",
      [client](const CLIArguments& args) -> Status {
        RETURN_NOT_OK_PREPEND(client->CompactSysCatalog(), "Unable to compact table sys_catalog");
        return Status::OK();
      });

  Register(
      "compact_table",
      " <table> [timeout_in_seconds] (default 20)"
      " [ADD_INDEXES] (default false)",
      [client](const CLIArguments& args) -> Status {
        bool add_indexes = false;
        int timeout_secs = 20;
        const auto table_name = VERIFY_RESULT(ResolveSingleTableName(
            client, args.begin(), args.end(),
            [&add_indexes, &timeout_secs](auto i, const auto& end) -> Status {
              std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(
                  GetTimeoutAndAddIndexesFlag(i, end));
              return Status::OK();
            }));
        // We use the same FlushTables RPC to trigger compaction.
        RETURN_NOT_OK_PREPEND(client->FlushTables({table_name},
                                                  add_indexes,
                                                  timeout_secs,
                                                  true /* is_compaction */),
                              Substitute("Unable to compact table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "compact_table_by_id", " <table_id> [timeout_in_seconds] (default 20)"
      " [ADD_INDEXES] (default false)",
      [client](const CLIArguments& args) -> Status {
        bool add_indexes = false;
        int timeout_secs = 20;
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        std::tie(timeout_secs, add_indexes) = VERIFY_RESULT(
            GetTimeoutAndAddIndexesFlag(args.begin() + 1, args.end()));
        // We use the same FlushTables RPC to trigger compaction.
        RETURN_NOT_OK_PREPEND(client->FlushTablesById({args[0]},
                                                      add_indexes,
                                                      timeout_secs,
                                                      true /* is_compaction */),
                              Substitute("Unable to compact table $0", args[0]));
        return Status::OK();
      });

  Register(
      "list_all_tablet_servers", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListAllTabletServers(FLAGS_exclude_dead),
                              "Unable to list tablet servers");
        return Status::OK();
      });

  Register(
      "list_all_masters", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListAllMasters(),
                              "Unable to list masters");
        return Status::OK();
      });

  Register(
      "change_master_config", " <ADD_SERVER|REMOVE_SERVER> <ip_addr> <port> [<uuid>]",
      [client](const CLIArguments& args) -> Status {
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
      });

  Register(
      "dump_masters_state", " [CONSOLE]",
      [client](const CLIArguments& args) -> Status {
        bool to_console = false;
        if (args.size() > 0) {
          if (IsEqCaseInsensitive(args[0], "CONSOLE")) {
            to_console = true;
          } else {
            return ClusterAdminCli::kInvalidArguments;
          }
        }
        RETURN_NOT_OK_PREPEND(client->DumpMasterState(to_console),
                              "Unable to dump master state");
        return Status::OK();
      });

  Register(
      "list_tablet_server_log_locations", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListTabletServersLogLocations(),
                              "Unable to list tablet server log locations");
        return Status::OK();
      });

  Register(
      "list_tablets_for_tablet_server", " <ts_uuid>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string& ts_uuid = args[0];
        RETURN_NOT_OK_PREPEND(client->ListTabletsForTabletServer(ts_uuid),
                              "Unable to list tablet server tablets");
        return Status::OK();
      });

  Register(
      "set_load_balancer_enabled", " <0|1>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const bool is_enabled = VERIFY_RESULT(CheckedStoi(args[0])) != 0;
        RETURN_NOT_OK_PREPEND(client->SetLoadBalancerEnabled(is_enabled),
                              "Unable to change load balancer state");
        return Status::OK();
      });

  Register(
      "get_load_balancer_state", "",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 0) {
          return ClusterAdminCli::kInvalidArguments;
        }

        RETURN_NOT_OK_PREPEND(client->GetLoadBalancerState(),
                              "Unable to get the load balancer state");
        return Status::OK();
      });

  Register(
      "get_load_move_completion", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->GetLoadMoveCompletion(),
                              "Unable to get load completion");
        return Status::OK();
      });

  Register(
      "get_leader_blacklist_completion", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->GetLeaderBlacklistCompletion(),
                              "Unable to get leader blacklist completion");
        return Status::OK();
      });

  Register(
      "get_is_load_balancer_idle", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->GetIsLoadBalancerIdle(),
                              "Unable to get is load balancer idle");
        return Status::OK();
      });

  Register(
      "list_leader_counts", " <table>",
      [client](const CLIArguments& args) -> Status {
        const auto table_name = VERIFY_RESULT(
            ResolveSingleTableName(client, args.begin(), args.end()));
        RETURN_NOT_OK_PREPEND(client->ListLeaderCounts(table_name),
                              "Unable to get leader counts");
        return Status::OK();
      });

  Register(
      "setup_redis_table", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->SetupRedisTable(),
                              "Unable to setup Redis keyspace and table");
        return Status::OK();
      });

  Register(
      "drop_redis_table", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->DropRedisTable(),
                              "Unable to drop Redis table");
        return Status::OK();
      });

  Register(
      "get_universe_config", "",
      std::bind(&GetUniverseConfig, client, _1));

  Register(
      "change_blacklist", Format(" <$0|$1> <ip_addr>:<port> [<ip_addr>:<port>]...",
          kBlacklistAdd, kBlacklistRemove),
      std::bind(&ChangeBlacklist, client, _1, false, "Unable to change blacklist"));

  Register(
      "change_leader_blacklist", Format(" <$0|$1> <ip_addr>:<port> [<ip_addr>:<port>]...",
          kBlacklistAdd, kBlacklistRemove),
      std::bind(&ChangeBlacklist, client, _1, true, "Unable to change leader blacklist"));

  Register(
      "master_leader_stepdown", " [dest_uuid]",
      std::bind(&MasterLeaderStepDown, client, _1));

  Register(
      "leader_stepdown", " <tablet_id> [dest_ts_uuid]",
      std::bind(&LeaderStepDown, client, _1));

  Register(
      "split_tablet", " <tablet_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string tablet_id = args[0];
        RETURN_NOT_OK_PREPEND(client->SplitTablet(tablet_id),
                              Format("Unable to start split of tablet $0", tablet_id));
        return Status::OK();
      });

  Register(
      "disable_tablet_splitting", " <disable_duration_ms> <feature_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const int64_t disable_duration_ms = VERIFY_RESULT(CheckedStoll(args[0]));
        const std::string feature_name = args[1];
        RETURN_NOT_OK_PREPEND(client->DisableTabletSplitting(disable_duration_ms, feature_name),
                              Format("Unable to disable tablet splitting for $0", feature_name));
        return Status::OK();
      });

  Register(
      "is_tablet_splitting_complete", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->IsTabletSplittingComplete(),
                              "Unable to check if tablet splitting is complete");
        return Status::OK();
      });

  Register(
      "create_transaction_table", " <table_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 1) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string table_name = args[0];
        RETURN_NOT_OK_PREPEND(client->CreateTransactionsStatusTable(table_name),
                              Format("Unable to create transaction table named $0", table_name));
        return Status::OK();
      });

  Register(
      "ysql_catalog_version", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->GetYsqlCatalogVersion(),
                              "Unable to get catalog version");
        return Status::OK();
      });

  RegisterJson("ddl_log", "", std::bind(&DdlLog, client, _1));

  Register(
      "upgrade_ysql", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->UpgradeYsql(),
                              "Unable to upgrade YSQL cluster");
        return Status::OK();
      });

  Register(
      // Today we have a weird pattern recognization for table name.
      // The expected input argument for the <table> is:
      // <db type>.<namespace> <table name>
      // (with a space in between).
      // So the expected arguement size is 3 (= 2 for the table name + 1 for the retention time).
      "set_wal_retention_secs", " <table> <seconds>", [client](const CLIArguments& args) -> Status {
        RETURN_NOT_OK(CheckArgumentsCount(args.size(), 3, 3));

        uint32_t wal_ret_secs = 0;
        const auto table_name = VERIFY_RESULT(
            ResolveSingleTableName(client, args.begin(), args.end(),
            [&wal_ret_secs] (auto i, const auto& end) -> Status {
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
            }
            ));
        RETURN_NOT_OK_PREPEND(
            client->SetWalRetentionSecs(table_name, wal_ret_secs),
            "Unable to set WAL retention time (sec) for the cluster");
        return Status::OK();
      });

  Register("get_wal_retention_secs", " <table>", [client](const CLIArguments& args) -> Status {
    RETURN_NOT_OK(CheckArgumentsCount(args.size(), 2, 2));
    const auto table_name = VERIFY_RESULT(ResolveSingleTableName(client, args.begin(), args.end()));
    RETURN_NOT_OK(client->GetWalRetentionSecs(table_name));
    return Status::OK();
  });
} // NOLINT, prevents long function message

Result<std::vector<client::YBTableName>> ResolveTableNames(
    ClusterAdminClientClass* client,
    CLIArgumentsIterator i,
    const CLIArgumentsIterator& end,
    const TailArgumentsProcessor& tail_processor,
    bool allow_namespace_only) {
  auto resolver = VERIFY_RESULT(client->BuildTableNameResolver());
  auto tail = i;
  // Greedy algorithm of taking as much tables as possible.
  for (; i != end; ++i) {
    const auto result = resolver.Feed(*i);
    if (!result.ok()) {
      // If tail arguments were not processed suppose it is bad table
      // and return its parsing error instead.
      if (tail_processor && tail_processor(tail, end).ok()) {
        break;
      }
      return result.status();
    }
    if (*result) {
      tail = std::next(i);
    }
  }

  auto& tables = resolver.values();
  // Handle case when no table name is followed keyspace.
  if (tail != end) {
    if (tail_processor) {
      RETURN_NOT_OK(tail_processor(tail, end));
    } else {
      if (allow_namespace_only && tables.empty()) {
        auto last_namespace = resolver.last_namespace();
        if (!last_namespace.name().empty()) {
          client::YBTableName table_name;
          table_name.GetFromNamespaceIdentifierPB(last_namespace);
          return std::vector<client::YBTableName>{table_name};
        }
      }
      return STATUS(InvalidArgument, "Table name is missed");
    }
  }
  if (tables.empty()) {
    return STATUS(InvalidArgument, "Empty list of tables");
  }
  return std::move(tables);
}

Result<client::YBTableName> ResolveSingleTableName(ClusterAdminClientClass* client,
                                                   CLIArgumentsIterator i,
                                                   const CLIArgumentsIterator& end,
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
  yb::Status s = yb::tools::enterprise::ClusterAdminCli().Run(argc, argv);
  if (s.ok()) {
    return 0;
  }

  if (s.IsInvalidArgument()) {
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
  }

  return 1;
}
