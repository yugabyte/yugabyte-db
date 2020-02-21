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

#include "yb/tools/yb-admin_cli.h"

#include <iostream>

#include <boost/algorithm/string.hpp>

#include "yb/tools/yb-admin_client.h"
#include "yb/util/tostring.h"

namespace yb {
namespace tools {
namespace enterprise {

using std::cerr;
using std::endl;
using std::string;
using std::vector;

using client::YBTableName;
using strings::Substitute;

void ClusterAdminCli::RegisterCommandHandlers(ClusterAdminClientClass* client) {
  super::RegisterCommandHandlers(client);

  Register(
      "list_snapshots", "",
      [client](const CLIArguments&) -> Status {
        RETURN_NOT_OK_PREPEND(client->ListSnapshots(),
                              "Unable to list snapshots");
        return Status::OK();
      });

  Register(
      "create_snapshot",
      " <keyspace> <table_name> [<keyspace> <table_name>]... [flush_timeout_in_seconds]"
      " (default 60, set 0 to skip flushing)",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 4) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const int num_tables = (args.size() - 2)/2;
        vector<YBTableName> tables;
        tables.reserve(num_tables);

        for (int i = 0; i < num_tables; ++i) {
          tables.push_back(VERIFY_RESULT(ResolveTableName(client, args[2 + i*2], args[3 + i*2])));
        }

        int timeout_secs = 60;
        if (args.size() % 2 == 1) {
          timeout_secs = std::stoi(args[args.size() - 1].c_str());
        }

        RETURN_NOT_OK_PREPEND(client->CreateSnapshot(tables, timeout_secs),
                              Substitute("Unable to create snapshot of tables: $0",
                                         yb::ToString(tables)));
        return Status::OK();
      });

  Register(
      "restore_snapshot", " <snapshot_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const string snapshot_id = args[2];
        RETURN_NOT_OK_PREPEND(client->RestoreSnapshot(snapshot_id),
                              Substitute("Unable to restore snapshot $0", snapshot_id));
        return Status::OK();
      });

  Register(
      "export_snapshot", " <snapshot_id> <file_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const string snapshot_id = args[2];
        const string file_name = args[3];
        RETURN_NOT_OK_PREPEND(client->CreateSnapshotMetaFile(snapshot_id, file_name),
                              Substitute("Unable to export snapshot $0 to file $1",
                                         snapshot_id,
                                         file_name));
        return Status::OK();
      });

  Register(
      "import_snapshot", " <file_name> [<keyspace> <table_name> [<keyspace> <table_name>]...]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3 || args.size() % 2 != 1) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const string file_name = args[2];
        const int num_tables = (args.size() - 3)/2;
        vector<YBTableName> tables;
        tables.reserve(num_tables);

        for (int i = 0; i < num_tables; ++i) {
          tables.push_back(YBTableName(
              VERIFY_RESULT(ParseNamespaceName(args[3 + i*2])).name,
              args[4 + i*2]));
        }

        string msg = num_tables > 0 ?
            Substitute("Unable to import tables $0 from snapshot meta file $1",
                       yb::ToString(tables), file_name) :
            Substitute("Unable to import snapshot meta file $0", file_name);

        RETURN_NOT_OK_PREPEND(client->ImportSnapshotMetaFile(file_name, tables), msg);
        return Status::OK();
      });

  Register(
      "delete_snapshot", " <snapshot_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }

        const string snapshot_id = args[2];
        RETURN_NOT_OK_PREPEND(client->DeleteSnapshot(snapshot_id),
                              Substitute("Unable to delete snapshot $0", snapshot_id));
        return Status::OK();
      });

  Register(
      "list_replica_type_counts", " <keyspace> <table_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const auto table_name = VERIFY_RESULT(ResolveTableName(client, args[2], args[3]));
        RETURN_NOT_OK_PREPEND(client->ListReplicaTypeCounts(table_name),
                              "Unable to list live and read-only replica counts");
        return Status::OK();
      });

  Register(
      "set_preferred_zones", " <cloud.region.zone> [<cloud.region.zone>]...",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->SetPreferredZones(
          std::vector<string>(args.begin() + 2, args.end())), "Unable to set preferred zones");
        return Status::OK();
      });

  Register(
      "rotate_universe_key", " key_path",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 2) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(
            client->RotateUniverseKey(args[2]), "Unable to rotate universe key.");
        return Status::OK();
      });

  Register(
      "disable_encryption", "",
      [client](const CLIArguments& args) -> Status {
        RETURN_NOT_OK_PREPEND(client->DisableEncryption(), "Unable to disable encryption.");
        return Status::OK();
      });

  Register(
      "is_encryption_enabled", "",
      [client](const CLIArguments& args) -> Status {
        RETURN_NOT_OK_PREPEND(client->IsEncryptionEnabled(), "Unable to get encryption status.");
        return Status::OK();
      });

  Register(
      "add_universe_key_to_all_masters", " key_id key_path",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          return ClusterAdminCli::kInvalidArguments;
        }
        string key_id = args[2];
        faststring contents;
        RETURN_NOT_OK(ReadFileToString(Env::Default(), args[3], &contents));
        string universe_key = contents.ToString();

        RETURN_NOT_OK_PREPEND(client->AddUniverseKeyToAllMasters(key_id, universe_key),
                              "Unable to add universe key to all masters.");
        return Status::OK();
      });

  Register(
      "all_masters_have_universe_key_in_memory", " key_id",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->AllMastersHaveUniverseKeyInMemory(args[2]),
                              "Unable to check whether master has universe key in memory.");
        return Status::OK();
      });

  Register(
      "rotate_universe_key_in_memory", " key_id",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        string key_id = args[2];

        RETURN_NOT_OK_PREPEND(client->RotateUniverseKeyInMemory(key_id),
                              "Unable rotate universe key in memory.");
        return Status::OK();
      });

  Register(
      "disable_encryption_in_memory", "",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 2) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->DisableEncryptionInMemory(), "Unable to disable encryption.");
        return Status::OK();
      });

  Register(
      "write_universe_key_to_file", " <key_id> <file_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          return ClusterAdminCli::kInvalidArguments;
        }
        RETURN_NOT_OK_PREPEND(client->WriteUniverseKeyToFile(args[2], args[3]),
                              "Unable to write key to file");
        return Status::OK();
      });

  Register(
      "create_cdc_stream", " <table_id>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string table_id = args[2];
        RETURN_NOT_OK_PREPEND(client->CreateCDCStream(table_id),
                              Substitute("Unable to create CDC stream for table $0", table_id));
        return Status::OK();
      });

  Register(
      "setup_universe_replication",
      " <producer_universe_uuid> <producer_master_addresses> <comma_separated_list_of_table_ids>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 5) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string producer_uuid = args[2];

        vector<string> producer_addresses;
        boost::split(producer_addresses, args[3], boost::is_any_of(","));

        vector<string> table_uuids;
        boost::split(table_uuids, args[4], boost::is_any_of(","));

        RETURN_NOT_OK_PREPEND(client->SetupUniverseReplication(producer_uuid,
                                                               producer_addresses,
                                                               table_uuids),
                              Substitute("Unable to setup replication from universe $0",
                                         producer_uuid));
        return Status::OK();
      });

  Register(
      "delete_universe_replication", " <producer_universe_uuid>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 3) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string producer_id = args[2];
        RETURN_NOT_OK_PREPEND(client->DeleteUniverseReplication(producer_id),
                              Substitute("Unable to delete replication for universe $0",
                              producer_id));
        return Status::OK();
      });

  Register(
      "set_universe_replication_enabled", " <producer_universe_uuid> <0|1>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() < 4) {
          return ClusterAdminCli::kInvalidArguments;
        }
        const string producer_id = args[2];
        const bool is_enabled = atoi(args[3].c_str()) != 0;
        RETURN_NOT_OK_PREPEND(client->SetUniverseReplicationEnabled(producer_id, is_enabled),
            Substitute("Unable to $0 replication for universe $1",
                is_enabled ? "enable" : "disable",
                producer_id));
        return Status::OK();
      });
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
