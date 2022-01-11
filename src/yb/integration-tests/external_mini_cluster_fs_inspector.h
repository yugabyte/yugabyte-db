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

#ifndef YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H
#define YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H

#include <functional>
#include <string>
#include <vector>

#include "yb/gutil/macros.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"

namespace yb {
class Env;
class ExternalMiniCluster;

namespace consensus {
class ConsensusMetadataPB;
}

namespace tablet {
class RaftGroupReplicaSuperBlockPB;
}

namespace itest {

// Utility class that digs around in a tablet server's data directory and
// provides methods useful for integration testing. This class must outlive
// the Env and ExternalMiniCluster objects that are passed into it.
class ExternalMiniClusterFsInspector {
 public:
  // Does not take ownership of the ExternalMiniCluster pointer.
  explicit ExternalMiniClusterFsInspector(ExternalMiniCluster* cluster);
  ~ExternalMiniClusterFsInspector();

  CHECKED_STATUS ListFilesInDir(const std::string& path, std::vector<std::string>* entries);
  size_t CountFilesInDir(const std::string& path);
  int CountWALSegmentsOnTS(int index);

  // List all of the tablets with tablet metadata in the cluster.
  std::vector<std::string> ListTablets();

  // List all of the tablets with tablet metadata on the given tablet server index.
  // This may include tablets that are tombstoned and not running.
  std::vector<std::string> ListTabletsOnTS(int index);

  // List all fs_data_roots with running tablets conut on the given tablet server index.
  std::unordered_map<std::string, std::vector<std::string>> DrivesOnTS(int index);

  // List the tablet IDs on the given tablet which actually have data (as
  // evidenced by their having a WAL). This excludes those that are tombstoned.
  std::vector<std::string> ListTabletsWithDataOnTS(int index);

  int CountWALSegmentsForTabletOnTS(int index, const std::string& tablet_id);
  bool DoesConsensusMetaExistForTabletOnTS(int index, const std::string& tablet_id);

  int CountReplicasInMetadataDirs();
  CHECKED_STATUS CheckNoDataOnTS(int index);
  CHECKED_STATUS CheckNoData();

  CHECKED_STATUS ReadTabletSuperBlockOnTS(int index, const std::string& tablet_id,
                                  tablet::RaftGroupReplicaSuperBlockPB* sb);

  // Get the modification time (in micros) of the tablet superblock for the given tablet
  // server index and tablet ID.
  int64_t GetTabletSuperBlockMTimeOrDie(int ts_index, const std::string& tablet_id);

  CHECKED_STATUS ReadConsensusMetadataOnTS(int index, const std::string& tablet_id,
                                   consensus::ConsensusMetadataPB* cmeta_pb);

  std::string GetTabletSuperBlockPathOnTS(int ts_index,
                                          const std::string& tablet_id) const;

  CHECKED_STATUS CheckTabletDataStateOnTS(int index,
                                  const std::string& tablet_id,
                                  tablet::TabletDataState state);

  CHECKED_STATUS WaitForNoData(const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  CHECKED_STATUS WaitForNoDataOnTS(
      int index, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  CHECKED_STATUS WaitForMinFilesInTabletWalDirOnTS(int index,
                                           const std::string& tablet_id,
                                           int count,
                                           const MonoDelta& timeout = MonoDelta::FromSeconds(60));
  CHECKED_STATUS WaitForReplicaCount(
      int expected, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  CHECKED_STATUS WaitForTabletDataStateOnTS(int index,
                                    const std::string& tablet_id,
                                    tablet::TabletDataState data_state,
                                    const MonoDelta& timeout = MonoDelta::FromSeconds(30));

  // Loop and check for certain filenames in the WAL directory of the specified
  // tablet. This function returns OK if we reach a state where:
  // * For each string in 'substrings_required', we find *at least one file*
  //   whose name contains that string, and:
  // * For each string in 'substrings_disallowed', we find *no files* whose name
  //   contains that string, even if the file also matches a string in the
  //   'substrings_required'.
  CHECKED_STATUS WaitForFilePatternInTabletWalDirOnTs(
      int ts_index,
      const std::string& tablet_id,
      const std::vector<std::string>& substrings_required,
      const std::vector<std::string>& substrings_disallowed,
      const MonoDelta& timeout = MonoDelta::FromSeconds(30));

 private:
  void TabletsWithDataOnTS(int index,
                           std::function<void (const std::string&, const std::string&)> handler);

  Env* const env_;
  ExternalMiniCluster* const cluster_;

  DISALLOW_COPY_AND_ASSIGN(ExternalMiniClusterFsInspector);
};

} // namespace itest
} // namespace yb

#endif // YB_INTEGRATION_TESTS_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H
