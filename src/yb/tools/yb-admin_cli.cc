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

#include "yb/tools/yb-admin_client.h"
#include "yb/util/flags.h"

#include "yb/master/master.proxy.h"

DEFINE_string(master_addresses, "localhost:7100",
              "Comma-separated list of YB Master server addresses");
DEFINE_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");

namespace yb {
namespace tools {

using std::cerr;
using std::endl;
using std::ostringstream;
using std::string;

using client::YBTableName;

int ClusterAdminCli::Run(int argc, char** argv) {
  const char* const prog_name = argv[0];
  FLAGS_logtostderr = 1;
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(prog_name);

  const string addrs = FLAGS_master_addresses;
  ClusterAdminClientClass client(addrs, FLAGS_timeout_ms);
  RegisterCommandHandlers(&client);
  SetUsage(prog_name);

  if (argc < 2) {
    UsageAndExit(prog_name);
  }

  // Find operation handler by operation name.
  const string op = argv[1];
  auto cmd = command_indexes_.find(op);

  if (cmd == command_indexes_.end()) {
    cerr << "Invalid operation: " << op << endl;
    UsageAndExit(prog_name);
  }

  // Init client.
  const Status s = client.Init();

  if (PREDICT_FALSE(!s.ok())) {
    cerr << s.CloneAndPrepend("Unable to establish connection to " + addrs).ToString() << endl;
    UsageAndExit(prog_name);
  }

  // Run found command.
  return commands_[cmd->second].fn_(argc, argv);
}

void ClusterAdminCli::UsageAndExit(const char* prog_name) {
  google::ShowUsageWithFlagsRestrict(prog_name, __FILE__);
  exit(1);
}

void ClusterAdminCli::Register(string&& cmd_name, string&& cmd_args, CommandFn&& cmd_fn) {
  command_indexes_[cmd_name] = commands_.size();
  commands_.push_back({std::move(cmd_name), std::move(cmd_args), std::move(cmd_fn)});
}

void ClusterAdminCli::SetUsage(const char* argv0) {
  ostringstream str;

  str << argv0 << " [-master_addresses server1,server2,server3] "
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
      [client](int argc, char** argv) -> int {
        if (argc < 5) {
          UsageAndExit(argv[0]);
        }
        const string tablet_id = argv[2];
        const string change_type = argv[3];
        const string peer_uuid = argv[4];
        boost::optional<string> member_type;
        if (argc > 5) {
          member_type = string(argv[5]);
        }
        const Status s = client->ChangeConfig(tablet_id, change_type, peer_uuid, member_type);
        if (!s.ok()) {
          cerr << "Unable to change config: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_tablet_servers", " <tablet_id>",
      [client](int argc, char** argv) -> int {
        if (argc < 3) {
          UsageAndExit(argv[0]);
        }
        const string tablet_id = argv[2];
        const Status s = client->ListPerTabletTabletServers(tablet_id);
        if (!s.ok()) {
          cerr << "Unable to list tablet servers of tablet " << tablet_id << ": " << s.ToString()
               << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_tables", "",
      [client](int, char**) -> int {
        const Status s = client->ListTables();
        if (!s.ok()) {
          cerr << "Unable to list tables: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_tablets", " <keyspace> <table_name> [max_tablets] (default 10, set 0 for max)",
      [client](int argc, char** argv) -> int {
        if (argc < 4) {
          UsageAndExit(argv[0]);
        }
        const YBTableName table_name(argv[2], argv[3]);
        int max = -1;
        if (argc > 4) {
          max = std::stoi(argv[4]);
        }
        const Status s = client->ListTablets(table_name, max);
        if (!s.ok()) {
          cerr << "Unable to list tablets of table " << table_name.ToString()
               << ": " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "delete_table", " <keyspace> <table_name>",
      [client](int argc, char** argv) -> int {
        if (argc != 4) {
          UsageAndExit(argv[0]);
        }
        const YBTableName table_name(argv[2], argv[3]);
        const Status s = client->DeleteTable(table_name);
        if (!s.ok()) {
          cerr << "Unable to delete table " << table_name.ToString()
                    << ": " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_all_tablet_servers", "",
      [client](int, char**) -> int {
        const Status s = client->ListAllTabletServers();
        if (!s.ok()) {
          cerr << "Unable to list tablet servers: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_all_masters", "",
      [client](int, char**) -> int {
        const Status s = client->ListAllMasters();
        if (!s.ok()) {
          cerr << "Unable to list masters: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "change_master_config", " <ADD_SERVER|REMOVE_SERVER> <ip_addr> <port>",
      [client](int argc, char** argv) -> int {
        int16 new_port = 0;
        string new_host;

        if (argc != 5) {
          UsageAndExit(argv[0]);
        }

        const string change_type = argv[2];
        if (change_type != "ADD_SERVER" && change_type != "REMOVE_SERVER") {
          UsageAndExit(argv[0]);
        }

        new_host = argv[3];
        new_port = atoi(argv[4]);

        const Status s = client->ChangeMasterConfig(change_type, new_host, new_port);
        if (!s.ok()) {
          cerr << "Unable to change master config: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "dump_masters_state", "",
      [client](int, char**) -> int {
        const Status s = client->DumpMasterState();
        if (!s.ok()) {
          cerr << "Unable to dump master state: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_tablet_server_log_locations", "",
      [client](int, char**) -> int {
        const Status s = client->ListTabletServersLogLocations();
        if (!s.ok()) {
          cerr << "Unable to list tablet server log locations: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_tablets_for_tablet_server", " <ts_uuid>",
      [client](int argc, char** argv) -> int {
        if (argc != 3) {
          UsageAndExit(argv[0]);
        }
        const string& ts_uuid = argv[2];
        const Status s = client->ListTabletsForTabletServer(ts_uuid);
        if (!s.ok()) {
          cerr << "Unable to list tablet server tablets: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "set_load_balancer_enabled", " <0|1>",
      [client](int argc, char** argv) -> int {
        if (argc != 3) {
          UsageAndExit(argv[0]);
        }

        const bool is_enabled = atoi(argv[2]) != 0;
        const Status s = client->SetLoadBalancerEnabled(is_enabled);
        if (!s.ok()) {
          cerr << "Unable to change load balancer state: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "get_load_move_completion", "",
      [client](int, char**) -> int {
        const Status s = client->GetLoadMoveCompletion();
        if (!s.ok()) {
          cerr << "Unable to get load completion: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "list_leader_counts", " <keyspace> <table_name>",
      [client](int argc, char** argv) -> int {
        if (argc != 4) {
          UsageAndExit(argv[0]);
        }
        const YBTableName table_name(argv[2], argv[3]);
        const Status s = client->ListLeaderCounts(table_name);
        if (!s.ok()) {
          cerr << "Unable to get leader counts: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "setup_redis_table", "",
      [client](int, char**) -> int {
        const Status s = client->SetupRedisTable();
        if (!s.ok()) {
          cerr << "Unable to setup Redis keyspace and table: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });

  Register(
      "drop_redis_table", "",
      [client](int, char**) -> int {
        const Status s = client->DropRedisTable();
        if (!s.ok()) {
          cerr << "Unable to drop Redis table: " << s.ToString() << endl;
          return 1;
        }
        return 0;
      });
}

}  // namespace tools
}  // namespace yb

int main(int argc, char** argv) {
  return yb::tools::YB_EDITION_NS_PREFIX ClusterAdminCli().Run(argc, argv);
}
