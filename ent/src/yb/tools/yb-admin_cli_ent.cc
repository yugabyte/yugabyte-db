// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_cli.h"

#include <iostream>

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
          UsageAndExit(args[0]);
        }

        const int num_tables = (args.size() - 2)/2;
        vector<YBTableName> tables(num_tables);

        for (int i = 0; i < num_tables; ++i) {
          tables[i].set_namespace_name(args[2 + i*2]);
          tables[i].set_table_name(args[3 + i*2]);
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
          UsageAndExit(args[0]);
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
          UsageAndExit(args[0]);
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
          UsageAndExit(args[0]);
        }

        const string file_name = args[2];
        const int num_tables = (args.size() - 3)/2;
        vector<YBTableName> tables(num_tables);

        for (int i = 0; i < num_tables; ++i) {
          tables[i].set_namespace_name(args[3 + i*2]);
          tables[i].set_table_name(args[4 + i*2]);
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
          UsageAndExit(args[0]);
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
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        RETURN_NOT_OK_PREPEND(client->ListReplicaTypeCounts(table_name),
                              "Unable to list live and read-only replica counts");
        return Status::OK();
      });
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
