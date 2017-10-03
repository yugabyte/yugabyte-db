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

#ifndef KUDU_INTEGRATION_TESTS_CLUSTER_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H_
#define KUDU_INTEGRATION_TESTS_CLUSTER_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H_

#include <string>
#include <vector>

#include "kudu/gutil/macros.h"
#include "kudu/tablet/metadata.pb.h"
#include "kudu/util/monotime.h"

namespace kudu {
class Env;
class ExternalMiniCluster;
class Status;

namespace consensus {
class ConsensusMetadataPB;
}

namespace tablet {
class TabletSuperBlockPB;
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

  Status ListFilesInDir(const std::string& path, std::vector<std::string>* entries);
  int CountFilesInDir(const std::string& path);
  int CountWALSegmentsOnTS(int index);

  // List all of the tablets with tablet metadata in the cluster.
  std::vector<std::string> ListTablets();

  // List all of the tablets with tablet metadata on the given tablet server index.
  // This may include tablets that are tombstoned and not running.
  std::vector<std::string> ListTabletsOnTS(int index);

  // List the tablet IDs on the given tablet which actually have data (as
  // evidenced by their having a WAL). This excludes those that are tombstoned.
  std::vector<std::string> ListTabletsWithDataOnTS(int index);

  int CountWALSegmentsForTabletOnTS(int index, const std::string& tablet_id);
  bool DoesConsensusMetaExistForTabletOnTS(int index, const std::string& tablet_id);

  int CountReplicasInMetadataDirs();
  Status CheckNoDataOnTS(int index);
  Status CheckNoData();

  Status ReadTabletSuperBlockOnTS(int index, const std::string& tablet_id,
                                  tablet::TabletSuperBlockPB* sb);
  Status ReadConsensusMetadataOnTS(int index, const std::string& tablet_id,
                                   consensus::ConsensusMetadataPB* cmeta_pb);
  Status CheckTabletDataStateOnTS(int index,
                                  const std::string& tablet_id,
                                  tablet::TabletDataState state);

  Status WaitForNoData(const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForNoDataOnTS(int index, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForMinFilesInTabletWalDirOnTS(int index,
                                           const std::string& tablet_id,
                                           int count,
                                           const MonoDelta& timeout = MonoDelta::FromSeconds(60));
  Status WaitForReplicaCount(int expected, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForTabletDataStateOnTS(int index,
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
  Status WaitForFilePatternInTabletWalDirOnTs(
      int ts_index,
      const std::string& tablet_id,
      const std::vector<std::string>& substrings_required,
      const std::vector<std::string>& substrings_disallowed,
      const MonoDelta& timeout = MonoDelta::FromSeconds(30));

 private:
  Env* const env_;
  ExternalMiniCluster* const cluster_;

  DISALLOW_COPY_AND_ASSIGN(ExternalMiniClusterFsInspector);
};

} // namespace itest
} // namespace kudu

#endif // KUDU_INTEGRATION_TESTS_CLUSTER_EXTERNAL_MINI_CLUSTER_FS_INSPECTOR_H_
