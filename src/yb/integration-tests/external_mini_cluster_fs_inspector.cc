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

#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"

#include <sys/types.h>

#include <filesystem>
#include <set>
#include <unordered_map>

#include "yb/consensus/metadata.pb.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"

namespace yb {
namespace itest {

using std::set;
using std::string;
using std::vector;

using consensus::ConsensusMetadataPB;
using strings::Substitute;
using tablet::TabletDataState;
using tablet::RaftGroupReplicaSuperBlockPB;

ExternalMiniClusterFsInspector::ExternalMiniClusterFsInspector(ExternalMiniCluster* cluster)
    : env_(Env::Default()),
      cluster_(CHECK_NOTNULL(cluster)) {
}

ExternalMiniClusterFsInspector::~ExternalMiniClusterFsInspector() {}

void ExternalMiniClusterFsInspector::TabletsWithDataOnTS(
    size_t index,
    std::function<void (const std::string&, const std::string&)> handler) {
  static const std::string kTabletDirPrefix = "tablet-";
  TableWalDirsOnTS(index, [&](const string& data_dir, const string& table_wal_dir) {
    vector<string> table_tablets;
    CHECK_OK(ListFilesInDir(table_wal_dir, &table_tablets));
    for (const auto& tablet : table_tablets) {
      CHECK(Slice(tablet).starts_with(kTabletDirPrefix));
      handler(data_dir, tablet.substr(kTabletDirPrefix.size()));
    }
  });
}

void ExternalMiniClusterFsInspector::TableWalDirsOnTS(size_t index,
  std::function<void (const string&, const string&)> handler) {
  for (const auto& data_dir : cluster_->tablet_server(index)->GetDataDirs()) {
    auto ts_wal_dir = JoinPathSegments(data_dir, FsManager::kWalDirName);
    vector<string> tables;
    CHECK_OK(ListFilesInDir(ts_wal_dir, &tables));
    for (const auto& table : tables) {
      handler(data_dir, JoinPathSegments(ts_wal_dir, table));
    }
  }
}

Result<vector<string>> ExternalMiniClusterFsInspector::ListTableWalFilesOnTS(
  size_t index, const TableId& table_id) {
  vector<string> wal_files;
  TableWalDirsOnTS(index, [&](const string& data_dir, const string& table_wal_dir) {
    if (table_wal_dir.find(table_id) == std::string::npos) {
      return;
    }
    vector<string> files;
    CHECK_OK(RecursivelyListFilesInDir(table_wal_dir, &files));
    for (const auto& file : files) {
      std::string filename = std::filesystem::path(file).filename();
      if (FsManager::IsWalSegmentFileName(filename)) {
        wal_files.push_back(file);
      }
    }
  });
  return wal_files;
}

Result<vector<string>> ExternalMiniClusterFsInspector::ListTableSstFilesOnTS(
  size_t index, const TableId& table_id) {
  vector<string> sst_files;
  for (const auto& data_dir : cluster_->tablet_server(index)->GetDataDirs()) {
    auto ts_rocksdb_dir = JoinPathSegments(data_dir,
                                           FsManager::kDataDirName,
                                           FsManager::kRocksDBDirName);
    vector<string> tables;
    RETURN_NOT_OK(ListFilesInDir(ts_rocksdb_dir, &tables));
    for (const auto& table : tables) {
      // Skip directories that do not correspond to the given table.
      if (table.find(table_id) == std::string::npos) {
        continue;
      }
      auto table_sst_dir = JoinPathSegments(ts_rocksdb_dir, table);
      vector<string> files = VERIFY_RESULT(RecursivelyListFilesInDir(table_sst_dir));
      for (const auto& file : files) {
        uint64_t number;
        rocksdb::FileType type;
        std::string filename = std::filesystem::path(file).filename();
        if (ParseFileName(filename, &number, &type) &&
            (type == rocksdb::kTableFile || type == rocksdb::kTableSBlockFile)) {
          sst_files.push_back(file);
        }
      }
      break;
    }
  }
  return sst_files;
}

Result<vector<string>> ExternalMiniClusterFsInspector::RecursivelyListFilesInDir(
  const string& path) {
  vector<string> entries;
  RETURN_NOT_OK(RecursivelyListFilesInDir(path, &entries));
  return entries;
}

Status ExternalMiniClusterFsInspector::RecursivelyListFilesInDir(const string& path,
                                                                 vector<string>* entries) {
  vector<string> files;
  RETURN_NOT_OK(ListFilesInDir(path, &files));
  for (const auto& file : files) {
    auto file_path = JoinPathSegments(path, file);
    if (VERIFY_RESULT(env_->IsDirectory(file_path))) {
      RETURN_NOT_OK(RecursivelyListFilesInDir(file_path, entries));
    } else {
      entries->push_back(std::move(file_path));
    }
  }
  return Status::OK();
}

Status ExternalMiniClusterFsInspector::ListFilesInDir(const string& path,
                                                      vector<string>* entries) {
  RETURN_NOT_OK(env_->GetChildren(path, entries));
  auto iter = entries->begin();
  while (iter != entries->end()) {
    if (*iter == "." || *iter == ".." || iter->find(".tmp.") != string::npos) {
      iter = entries->erase(iter);
      continue;
    }
    ++iter;
  }
  return Status::OK();
}

size_t ExternalMiniClusterFsInspector::CountFilesInDir(const string& path) {
  vector<string> entries;
  Status s = ListFilesInDir(path, &entries);
  if (!s.ok()) return 0;
  return entries.size();
}

int ExternalMiniClusterFsInspector::CountWALSegmentsOnTS(size_t index) {
  int total_segments = 0;
  TableWalDirsOnTS(index, [&](const string& data_dir, const string& table_wal_dir) {
    vector<string> tablets;
    CHECK_OK(ListFilesInDir(table_wal_dir, &tablets));
    for (const string &tablet : tablets) {
      string tablet_wal_dir = JoinPathSegments(table_wal_dir, tablet);
      total_segments += CountFilesInDir(tablet_wal_dir);
    }
  });
  return total_segments;
}

vector<string> ExternalMiniClusterFsInspector::ListTablets() {
  set<string> tablets;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto ts_tablets = ListTabletsOnTS(i);
    tablets.insert(ts_tablets.begin(), ts_tablets.end());
  }
  return vector<string>(tablets.begin(), tablets.end());
}

vector<string> ExternalMiniClusterFsInspector::ListTabletsOnTS(size_t index) {
  vector<string> all_tablets;
  for (const auto& data_dir : cluster_->tablet_server(index)->GetDataDirs()) {
    string meta_dir = FsManager::GetRaftGroupMetadataDir(data_dir);
    vector<string> tablets;
    CHECK_OK(ListFilesInDir(meta_dir, &tablets));
    std::copy(tablets.begin(), tablets.end(), std::back_inserter(all_tablets));
  }
  return all_tablets;
}

std::unordered_map<string, vector<string>> ExternalMiniClusterFsInspector::DrivesOnTS(
    size_t index) {
  std::unordered_map<string, vector<string>> drives;
  TabletsWithDataOnTS(index, [&](const std::string& drive, const std::string& tablet) {
    drives[drive].push_back(tablet);
  });
  return drives;
}

vector<string> ExternalMiniClusterFsInspector::ListTabletsWithDataOnTS(size_t index) {
  vector<string> all_tablets;
  TabletsWithDataOnTS(index, [&](const std::string& drive, const std::string& tablet) {
    all_tablets.push_back(tablet);
  });
  return all_tablets;
}

int ExternalMiniClusterFsInspector::CountWALSegmentsForTabletOnTS(size_t index,
                                                                  const string& tablet_id) {
  int count = 0;
  TableWalDirsOnTS(index, [&](const string& data_dir, const string& table_wal_dir) {
    vector<string> tablets;
    CHECK_OK(ListFilesInDir(table_wal_dir, &tablets));
      for (const auto& tablet : tablets) {
        // All tablets wal directories start with the string 'tablet-'
        if (tablet == Substitute("tablet-$0", tablet_id)) {
          auto tablet_wal_dir = JoinPathSegments(table_wal_dir, tablet);
          count += CountFilesInDir(tablet_wal_dir);
        }
      }
    });
  return count;
}

bool ExternalMiniClusterFsInspector::DoesConsensusMetaExistForTabletOnTS(size_t index,
                                                                         const string& tablet_id) {
  ConsensusMetadataPB cmeta_pb;
  Status s = ReadConsensusMetadataOnTS(index, tablet_id, &cmeta_pb);
  return s.ok();
}

int ExternalMiniClusterFsInspector::CountReplicasInMetadataDirs() {
  // Rather than using FsManager's functionality for listing blocks, we just manually
  // list the contents of the metadata directory. This is because we're using an
  // external minicluster, and initializing a new FsManager to point at the running
  // tablet servers isn't easy.
  int count = 0;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    for (const auto& data_dir : cluster_->tablet_server(i)->GetDataDirs()) {
      count += CountFilesInDir(FsManager::GetRaftGroupMetadataDir(data_dir));
    }
  }
  return count;
}

Status ExternalMiniClusterFsInspector::CheckNoDataOnTS(size_t index) {
  for (const auto& data_dir : cluster_->tablet_server(index)->GetDataDirs()) {
    if (CountFilesInDir(FsManager::GetRaftGroupMetadataDir(data_dir)) > 0) {
      return STATUS(IllegalState, "tablet metadata blocks still exist", data_dir);
    }
    if (CountWALSegmentsOnTS(index) > 0) {
      return STATUS(IllegalState, "wals still exist", data_dir);
    }
    if (CountFilesInDir(FsManager::GetConsensusMetadataDir(data_dir)) > 0) {
      return STATUS(IllegalState, "consensus metadata still exists", data_dir);
    }
  }
  return Status::OK();
}

Status ExternalMiniClusterFsInspector::CheckNoData() {
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    RETURN_NOT_OK(CheckNoDataOnTS(i));
  }
  return Status::OK();;
}

std::string ExternalMiniClusterFsInspector::GetTabletSuperBlockPathOnTS(
    size_t ts_index, const string& tablet_id) const {
  string data_dir = cluster_->tablet_server(ts_index)->GetDataDirs()[0];
  std::string meta_dir = FsManager::GetRaftGroupMetadataDir(data_dir);
  return JoinPathSegments(meta_dir, tablet_id);
}

Status ExternalMiniClusterFsInspector::ReadTabletSuperBlockOnTS(size_t index,
                                                                const string& tablet_id,
                                                                RaftGroupReplicaSuperBlockPB* sb) {
  const auto& sb_path = GetTabletSuperBlockPathOnTS(index, tablet_id);
  return pb_util::ReadPBContainerFromPath(env_, sb_path, sb);
}

int64_t ExternalMiniClusterFsInspector::GetTabletSuperBlockMTimeOrDie(
    size_t ts_index, const std::string& tablet_id) {
  const auto& sb_path = GetTabletSuperBlockPathOnTS(ts_index, tablet_id);
  struct stat s;
  CHECK_ERR(stat(sb_path.c_str(), &s)) << "failed to stat: " << sb_path;
#ifdef __APPLE__
  return s.st_mtimespec.tv_sec * 1e6 + s.st_mtimespec.tv_nsec / 1000;
#else
  return s.st_mtim.tv_sec * 1e6 + s.st_mtim.tv_nsec / 1000;
#endif
}

Status ExternalMiniClusterFsInspector::ReadConsensusMetadataOnTS(size_t index,
                                                                 const string& tablet_id,
                                                                 ConsensusMetadataPB* cmeta_pb) {
  for (const auto& data_dir : cluster_->tablet_server(index)->GetDataDirs()) {
    string cmeta_dir = FsManager::GetConsensusMetadataDir(data_dir);
    string cmeta_file = JoinPathSegments(cmeta_dir, tablet_id);
    if (env_->FileExists(cmeta_file)) {
      return pb_util::ReadPBContainerFromPath(env_, cmeta_file, cmeta_pb);
    }
  }
  return STATUS(NotFound, "Consensus metadata file not found");
}

Status ExternalMiniClusterFsInspector::CheckTabletDataStateOnTS(size_t index,
                                                                const string& tablet_id,
                                                                TabletDataState state) {
  RaftGroupReplicaSuperBlockPB sb;
  RETURN_NOT_OK(ReadTabletSuperBlockOnTS(index, tablet_id, &sb));
  if (PREDICT_FALSE(sb.tablet_data_state() != state)) {
    return STATUS(IllegalState, "Tablet data state != " + TabletDataState_Name(state),
                                TabletDataState_Name(sb.tablet_data_state()));
  }
  return Status::OK();
}

Status ExternalMiniClusterFsInspector::WaitForNoData(const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  Status s;
  while (true) {
    s = CheckNoData();
    if (s.ok()) return Status::OK();
    if (deadline.ComesBefore(MonoTime::Now())) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return STATUS(TimedOut, "Timed out waiting for no data", s.ToString());
}

Status ExternalMiniClusterFsInspector::WaitForNoDataOnTS(size_t index, const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  Status s;
  while (true) {
    s = CheckNoDataOnTS(index);
    if (s.ok()) return Status::OK();
    if (deadline.ComesBefore(MonoTime::Now())) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return STATUS(TimedOut, "Timed out waiting for no data", s.ToString());
}

Status ExternalMiniClusterFsInspector::WaitForMinFilesInTabletWalDirOnTS(size_t index,
                                                                         const string& tablet_id,
                                                                         int count,
                                                                         const MonoDelta& timeout) {
  int seen = 0;
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  while (true) {
    seen = CountWALSegmentsForTabletOnTS(index, tablet_id);
    if (seen >= count) return Status::OK();
    if (deadline.ComesBefore(MonoTime::Now())) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return STATUS(TimedOut, Substitute("Timed out waiting for number of WAL segments on tablet $0 "
                                     "on TS $1 to be $2. Found $3",
                                     tablet_id, index, count, seen));
}

Status ExternalMiniClusterFsInspector::WaitForReplicaCount(int expected, const MonoDelta& timeout) {
  Status s;
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  int found;
  while (true) {
    found = CountReplicasInMetadataDirs();
    if (found == expected) return Status::OK();
    if (CountReplicasInMetadataDirs() == expected) return Status::OK();
    if (deadline.ComesBefore(MonoTime::Now())) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  return STATUS(TimedOut, Substitute("Timed out waiting for a total replica count of $0. "
                                     "Found $1 replicas",
                                     expected, found));
}

Status ExternalMiniClusterFsInspector::WaitForTabletDataStateOnTS(size_t index,
                                                                  const string& tablet_id,
                                                                  TabletDataState expected,
                                                                  const MonoDelta& timeout) {
  MonoTime start = MonoTime::Now();
  MonoTime deadline = start;
  deadline.AddDelta(timeout);
  Status s;
  while (true) {
    s = CheckTabletDataStateOnTS(index, tablet_id, expected);
    if (s.ok()) return Status::OK();
    if (deadline.ComesBefore(MonoTime::Now())) break;
    SleepFor(MonoDelta::FromMilliseconds(5));
  }
  return STATUS(TimedOut, Substitute(
      "Timed out after $0 waiting for tablet $1 on TS-$2 to be in data state $3: $4",
      MonoTime::Now().GetDeltaSince(start).ToString(),
      tablet_id,
      index + 1,
      TabletDataState_Name(expected),
      s.ToString()));
}


Status ExternalMiniClusterFsInspector::WaitForFilePatternInTabletWalDirOnTs(
    int ts_index, const string& tablet_id,
    const vector<string>& substrings_required,
    const vector<string>& substrings_disallowed,
    const MonoDelta& timeout) {
  Status s;
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);

  auto dirs = cluster_->tablet_server(ts_index)->GetDataDirs();
  if (dirs.size() != 1) {
    return STATUS(IllegalState, "Multi drive is not supported");
  }
  string data_dir = dirs[0];
  string ts_wal_dir = JoinPathSegments(data_dir, FsManager::kWalDirName);
  vector<string> tables;
  RETURN_NOT_OK_PREPEND(ListFilesInDir(ts_wal_dir, &tables),
                        Substitute("Unable to list files from directory $0", ts_wal_dir));

  string table_wal_dir = JoinPathSegments(ts_wal_dir, tables[0]);
  string tablet_wal_dir = JoinPathSegments(table_wal_dir, Substitute("tablet-$0", tablet_id));

  string error_msg;
  vector<string> entries;
  while (true) {
    RETURN_NOT_OK(ListFilesInDir(tablet_wal_dir, &entries));
    std::sort(entries.begin(), entries.end());

    error_msg = "";
    bool any_missing_required = false;
    for (const string& required_filter : substrings_required) {
      bool filter_matched = false;
      for (const string& entry : entries) {
        if (entry.find(required_filter) != string::npos) {
          filter_matched = true;
          break;
        }
      }
      if (!filter_matched) {
        any_missing_required = true;
        error_msg += "missing from substrings_required: " + required_filter + "; ";
        break;
      }
    }

    bool any_present_disallowed = false;
    for (const string& entry : entries) {
      if (any_present_disallowed) break;
      for (const string& disallowed_filter : substrings_disallowed) {
        if (entry.find(disallowed_filter) != string::npos) {
          any_present_disallowed = true;
          error_msg += "present from substrings_disallowed: " + entry +
                       " (" + disallowed_filter + "); ";
          break;
        }
      }
    }

    if (!any_missing_required && !any_present_disallowed) {
      return Status::OK();
    }
    if (deadline.ComesBefore(MonoTime::Now())) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  return STATUS(TimedOut, Substitute("Timed out waiting for file pattern on "
                                     "tablet $0 on TS $1 in directory $2",
                                     tablet_id, ts_index, tablet_wal_dir),
                          error_msg + "entries: " + JoinStrings(entries, ", "));
}

} // namespace itest
} // namespace yb
