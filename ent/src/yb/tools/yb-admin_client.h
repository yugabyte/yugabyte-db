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

  CHECKED_STATUS IsEncryptionEnabled();

  CHECKED_STATUS AddUniverseKeyToAllMasters(
      const std::string& key_id, const std::string& universe_key);

  CHECKED_STATUS AllMastersHaveUniverseKeyInMemory(const std::string& key_id);

  CHECKED_STATUS RotateUniverseKeyInMemory(const std::string& key_id);

  CHECKED_STATUS DisableEncryptionInMemory();

  CHECKED_STATUS CreateCDCStream(const TableId& table_id);

  CHECKED_STATUS SetupUniverseReplication(const std::string& producer_uuid,
                                          const std::vector<std::string>& producer_addresses,
                                          const std::vector<TableId>& tables);

  CHECKED_STATUS DeleteUniverseReplication(const std::string& producer_id);

  CHECKED_STATUS SetUniverseReplicationEnabled(const std::string& producer_id,
                                               bool is_enabled);

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
