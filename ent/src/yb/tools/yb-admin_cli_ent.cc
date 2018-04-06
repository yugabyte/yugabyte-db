// Copyright (c) YugaByte, Inc.

#include "yb/tools/yb-admin_cli.h"

#include <iostream>

#include "yb/tools/yb-admin_client.h"

namespace yb {
namespace tools {
namespace enterprise {

using std::cerr;
using std::endl;
using std::string;

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
      " <keyspace> <table_name> [flush_timeout_in_seconds] (default 60, set 0 to skip flushing)",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4 && args.size() != 5) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        int timeout_secs = 60;
        if (args.size() > 4) {
          timeout_secs = std::stoi(args[4].c_str());
        }

        RETURN_NOT_OK_PREPEND(client->CreateSnapshot(table_name, timeout_secs),
                              Substitute("Unable to create snapshot of table $0",
                                         table_name.ToString()));
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
      "import_snapshot", " <file_name> [<keyspace> <table_name>]",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3 && args.size() != 5) {
          UsageAndExit(args[0]);
        }

        const string file_name = args[2];
        YBTableName table_name;
        if (args.size() >= 5) {
          table_name.set_namespace_name(args[3]);
          table_name.set_table_name(args[4]);
        }
        RETURN_NOT_OK_PREPEND(client->ImportSnapshotMetaFile(file_name, table_name),
                              Substitute("Unable to import snapshot meta file $0", file_name));
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
