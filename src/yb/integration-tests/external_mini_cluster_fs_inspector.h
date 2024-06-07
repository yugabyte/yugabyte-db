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

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "yb/common/entity_ids_types.h"

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

  Result<std::vector<std::string>> RecursivelyListFilesInDir(const std::string& path);
  Status RecursivelyListFilesInDir(const std::string& path, std::vector<std::string>* entries);
  Status ListFilesInDir(const std::string& path, std::vector<std::string>* entries);
  size_t CountFilesInDir(const std::string& path);
  int CountWALSegmentsOnTS(size_t index);

  // List all of the tablets with tablet metadata in the cluster.
  std::vector<std::string> ListTablets();

  // List all of the tablets with tablet metadata on the given tablet server index.
  // This may include tablets that are tombstoned and not running.
  std::vector<std::string> ListTabletsOnTS(size_t index);

  void TableWalDirsOnTS(size_t index,
    std::function<void (const std::string&, const std::string&)> handler);
  Result<std::vector<std::string>> ListTableWalFilesOnTS(size_t index, const TableId& table_id);
  Result<std::vector<std::string>> ListTableSstFilesOnTS(size_t index, const TableId& table_id);

  // List all fs_data_roots with running tablets conut on the given tablet server index.
  std::unordered_map<std::string, std::vector<std::string>> DrivesOnTS(size_t index);

  // List the tablet IDs on the given tablet which actually have data (as
  // evidenced by their having a WAL). This excludes those that are tombstoned.
  std::vector<std::string> ListTabletsWithDataOnTS(size_t index);

  int CountWALSegmentsForTabletOnTS(size_t index, const std::string& tablet_id);
  bool DoesConsensusMetaExistForTabletOnTS(size_t index, const std::string& tablet_id);

  int CountReplicasInMetadataDirs();
  Status CheckNoDataOnTS(size_t index);
  Status CheckNoData();

  Status ReadTabletSuperBlockOnTS(
      size_t index, const std::string& tablet_id, tablet::RaftGroupReplicaSuperBlockPB* sb);

  // Get the modification time (in micros) of the tablet superblock for the given tablet
  // server index and tablet ID.
  int64_t GetTabletSuperBlockMTimeOrDie(size_t ts_index, const std::string& tablet_id);

  Status ReadConsensusMetadataOnTS(
      size_t index, const std::string& tablet_id, consensus::ConsensusMetadataPB* cmeta_pb);

  std::string GetTabletSuperBlockPathOnTS(size_t ts_index, const std::string& tablet_id) const;

  Status CheckTabletDataStateOnTS(
      size_t index, const std::string& tablet_id, tablet::TabletDataState state);

  Status WaitForNoData(const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForNoDataOnTS(
      size_t index, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForMinFilesInTabletWalDirOnTS(
      size_t index,
      const std::string& tablet_id,
      int count,
      const MonoDelta& timeout = MonoDelta::FromSeconds(60));
  Status WaitForReplicaCount(
      int expected, const MonoDelta& timeout = MonoDelta::FromSeconds(30));
  Status WaitForTabletDataStateOnTS(size_t index,
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
  void TabletsWithDataOnTS(
      size_t index, std::function<void (const std::string&, const std::string&)> handler);

  Env* const env_;
  ExternalMiniCluster* const cluster_;

  DISALLOW_COPY_AND_ASSIGN(ExternalMiniClusterFsInspector);
};

} // namespace itest
} // namespace yb
