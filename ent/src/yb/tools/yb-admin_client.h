// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H
#define ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H

#include "../../../../src/yb/tools/yb-admin_client.h"

#include "yb/master/master_backup.proxy.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/secure.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"

namespace yb {
namespace tools {
namespace enterprise {

class ClusterAdminClient : public yb::tools::ClusterAdminClient {
  typedef yb::tools::ClusterAdminClient super;
 public:
  ClusterAdminClient(std::string addrs, int64_t timeout_millis, std::string certs_dir)
      : super(std::move(addrs), timeout_millis, certs_dir),
        certs_dir_(std::move(certs_dir)) {}

  // Initialized the client and connects to the server service proxies.
  CHECKED_STATUS Init() override;

  // Snapshot operations.
  CHECKED_STATUS ListSnapshots();
  CHECKED_STATUS CreateSnapshot(const std::vector<client::YBTableName>& tables,
                                int flush_timeout_secs);
  CHECKED_STATUS RestoreSnapshot(const std::string& snapshot_id);
  CHECKED_STATUS DeleteSnapshot(const std::string& snapshot_id);

  CHECKED_STATUS CreateSnapshotMetaFile(const std::string& snapshot_id,
                                        const std::string& file_name);
  CHECKED_STATUS ImportSnapshotMetaFile(const std::string& file_name,
                                        const std::vector<client::YBTableName>& tables);
  CHECKED_STATUS ListReplicaTypeCounts(const client::YBTableName& table_name);

  CHECKED_STATUS SetPreferredZones(const std::vector<string>& preferred_zones);

  CHECKED_STATUS RotateUniverseKey(const std::string& key_path);

  CHECKED_STATUS DisableEncryption();

 private:

  CHECKED_STATUS SendEncryptionRequest(const std::string& key_path, bool enable_encryption);

  std::unique_ptr<master::MasterBackupServiceProxy> master_backup_proxy_;

  // Secure connection info to connect with tls enabled servers.
  std::string certs_dir_;
  std::unique_ptr<rpc::SecureContext> secure_context_;

  DISALLOW_COPY_AND_ASSIGN(ClusterAdminClient);
};

}  // namespace enterprise
}  // namespace tools
}  // namespace yb

#endif // ENT_SRC_YB_TOOLS_YB_ADMIN_CLIENT_H
