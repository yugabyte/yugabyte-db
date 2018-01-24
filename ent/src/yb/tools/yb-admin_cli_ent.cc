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
      "create_snapshot", " <keyspace> <table_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 4) {
          UsageAndExit(args[0]);
        }
        const YBTableName table_name(args[2], args[3]);
        RETURN_NOT_OK_PREPEND(client->CreateSnapshot(table_name),
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
      "import_snapshot", " <file_name>",
      [client](const CLIArguments& args) -> Status {
        if (args.size() != 3) {
          UsageAndExit(args[0]);
        }

        const string file_name = args[2];
        RETURN_NOT_OK_PREPEND(client->ImportSnapshotMetaFile(file_name),
                              Substitute("Unable to import snapshot meta file $0", file_name));
        return Status::OK();
      });
}

}  // namespace enterprise
}  // namespace tools
}  // namespace yb
