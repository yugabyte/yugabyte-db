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

#include <iostream>

#include <boost/lexical_cast.hpp>
#include <boost/range.hpp>

#include "yb/tools/yb-admin_client.h"
#include "yb/util/flags.h"

DEFINE_string(master_addresses, "localhost:7100",
              "Comma-separated list of YB Master server addresses");
DEFINE_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");

namespace yb {
namespace tools {
namespace {

const int32 kDefaultRpcPort = 9100;
const string kBlacklistAdd("ADD");
const string kBlacklistRemove("REMOVE");

void UsageAndExit(const ClusterAdminCli::CLIArguments& args) {
  ClusterAdminCli::UsageAndExit(args[0]);
}

CHECKED_STATUS GetUniverseConfig(ClusterAdminClientClass* client,
                                 const ClusterAdminCli::CLIArguments&) {
  RETURN_NOT_OK_PREPEND(client->GetUniverseConfig(), "Unable to get universe config");
  return Status::OK();
}

CHECKED_STATUS ChangeBlacklist(ClusterAdminClientClass* client,
                               const ClusterAdminCli::CLIArguments& args) {
  if (args.size() < 4) {
    UsageAndExit(args);
  }
  const auto change_type = args[2];
  if (change_type != kBlacklistAdd && change_type != kBlacklistRemove) {
    UsageAndExit(args);
  }
  std::vector<HostPort> hostports;
  for (const auto& arg : boost::make_iterator_range(args.begin() + 3, args.end())) {
    hostports.push_back(VERIFY_RESULT(HostPort::FromString(arg, kDefaultRpcPort)));
  }
  RETURN_NOT_OK_PREPEND(client->ChangeBlacklist(hostports, change_type == kBlacklistAdd),
                        "Unable to change blacklist");
  return Status::OK();
}

} // namespace

using std::cerr;
using std::endl;
using std::ostringstream;
using std::string;

using client::YBTableName;
using strings::Substitute;

using namespace std::placeholders;

int ClusterAdminCli::Run(int argc, char** argv) {
  const string prog_name = argv[0];
  FLAGS_logtostderr = 1;
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(prog_name.c_str());

  const string addrs = FLAGS_master_addresses;
  ClusterAdminClientClass client(addrs, FLAGS_timeout_ms);
  RegisterCommandHandlers(&client);
  SetUsage(prog_name);

  CLIArguments args;
  for (int i = 0; i < argc; ++i) {
    args.push_back(argv[i]);
  }

  if (args.size() < 2) {
    UsageAndExit(prog_name);
  }

  // Find operation handler by operation name.
  const string op = args[1];
  auto cmd = command_indexes_.find(op);

  if (cmd == command_indexes_.end()) {
    cerr << "Invalid operation: " << op << endl;
    UsageAndExit(prog_name);
  }

  // Init client.
  Status s = client.Init();

  if (PREDICT_FALSE(!s.ok())) {
    cerr << s.CloneAndPrepend("Unable to establish connection to " + addrs).ToString() << endl;
    UsageAndExit(prog_name);
  }

  // Run found command.
  s = commands_[cmd->second].fn_(args);
  if (!s.ok()) {
    cerr << "Error: " << s.ToString() << endl;
    return 1;
  }
  return 0;
}

void ClusterAdminCli::UsageAndExit(const string& prog_name) {
  google::ShowUsageWithFlagsRestrict(prog_name.c_str(), __FILE__);
  exit(1);
}

void ClusterAdminCli::Register(string&& cmd_name, string&& cmd_args, CommandFn&& cmd_fn) {
  command_indexes_[cmd_name] = commands_.size();
  commands_.push_back({std::move(cmd_name), std::move(cmd_args), std::move(cmd_fn)});
}

void ClusterAdminCli::SetUsage(const string& prog_name) {
  ostringstream str;

  str << prog_name << " [-master_addresses server1,server2,server3] "
      << " [-timeout_ms <millisec>] <operation>" << endl
      << "<operation> must be one of:" << endl;

  for (size_t i = 0; i < commands_.size(); ++i) {
    str << ' ' << i + 1 << ". " << commands_[i].name_ << commands_[i].usage_arguments_ << endl;
  }

  google::SetUsageMessage(str.str());
}

void ClusterAdminCli::RegisterCommandHandlers(ClusterAdminClientClass* client) {
  DCHECK_ONLY_NOTNULL(client);

  Register(
      "change_config",
      " <tablet_id> <ADD_SERVER|REMOVE_SERVER> <peer_uuid> [PRE_VOTER|PRE_OBSERVER]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 5) {
          UsageAndExit(args[0]);
        }
        const string tablet_id = args[2];
        const string change_type = args[3];
        const string peer_uuid = args[4];
        boost::optional<string> member_type;
        if (args.size() > 5) {
          member_type = args[5];
        }
        RETURN_NOT_OK_PREPEND(client->ChangeConfig(tablet_id, change_type, peer_uuid, member_type),
                              "Unable to change config");
        return Status::OK();
      });

  Register(
      "list_tablet_servers", " <tablet_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3) {
          UsageAndExit(args[0]);
        }
        const string tablet_id = args[2];
        RETURN_NOT_OK_PREPEND(client->ListPerTabletTabletServers(tablet_id),
                              Substitute("Unable to list tablet servers of tablet $0", tablet_id));
        return Status::OK();
      });

  Register(
      "list_tables", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListTables(),
                              "Unable to list tables");
        return Status::OK();
      });

  Register(
      "list_tablets", " <keyspace> <table_name> [max_tablets] (default 10, set 0 for max)",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 4) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        int max = -1;
        if (args.size() > 4) {
          max = std::stoi(args[4].c_str());
        }
        RETURN_NOT_OK_PREPEND(client->ListTablets(table_name, max),
                              Substitute("Unable to list tablets of table $0",
                                         table_name.ToString()));
        return Status::OK();
      });

  Register(
      "modify_placement_info", " <placement_info> <replication_factor>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          UsageAndExit(args[0]);
        }
        int rf = boost::lexical_cast<int>(args[3]);
        RETURN_NOT_OK_PREPEND(client->ModifyPlacementInfo(args[2], rf),
                              Substitute("Unable to modify placement info."));
        return Status::OK();
      });

  Register(
      "delete_table", " <keyspace> <table_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        RETURN_NOT_OK_PREPEND(client->DeleteTable(table_name),
                              Substitute("Unable to delete table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "flush_table", " <keyspace> <table_name> [timeout_in_seconds] (default 20)",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4 && args.size() != 5) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        int timeout_secs = 20;
        if (args.size() > 4) {
          timeout_secs = std::stoi(args[4].c_str());
        }

        RETURN_NOT_OK_PREPEND(client->FlushTable(table_name, timeout_secs),
                              Substitute("Unable to flush table $0", table_name.ToString()));
        return Status::OK();
      });

  Register(
      "list_all_tablet_servers", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListAllTabletServers(),
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
      "change_master_config", " <ADD_SERVER|REMOVE_SERVER> <ip_addr> <port> <0|1>",
      [client](const CLIArguments& args) -> Status {
        int16 new_port = 0;
        string new_host;

        if (args.size() != 5 && args.size() != 6) {
          UsageAndExit(args[0]);
        }

        const string change_type = args[2];
        if (change_type != "ADD_SERVER" && change_type != "REMOVE_SERVER") {
          UsageAndExit(args[0]);
        }

        new_host = args[3];
        new_port = atoi(args[4].c_str());

        bool use_hostport = false;
        if (args.size() == 6) {
          use_hostport = atoi(args[5].c_str()) != 0;
        }
        RETURN_NOT_OK_PREPEND(client->ChangeMasterConfig(change_type, new_host, new_port,
                                                         use_hostport),
                              "Unable to change master config");
        return Status::OK();
      });

  Register(
      "dump_masters_state", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->DumpMasterState(),
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
        if (args.size() != 3) {
          UsageAndExit(args[0]);
        }
        const string& ts_uuid = args[2];
        RETURN_NOT_OK_PREPEND(client->ListTabletsForTabletServer(ts_uuid),
                              "Unable to list tablet server tablets");
        return Status::OK();
      });

  Register(
      "set_load_balancer_enabled", " <0|1>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          UsageAndExit(args[0]);
        }

        const bool is_enabled = atoi(args[2].c_str()) != 0;
        RETURN_NOT_OK_PREPEND(client->SetLoadBalancerEnabled(is_enabled),
                              "Unable to change load balancer state");
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
      "list_leader_counts", " <keyspace> <table_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
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
      std::bind(&ChangeBlacklist, client, _1));
}

}  // namespace tools
}  // namespace yb

int main(int argc, char** argv) {
  return yb::tools::YB_EDITION_NS_PREFIX ClusterAdminCli().Run(argc, argv);
}
