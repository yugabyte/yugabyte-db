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

#include "yb/tools/fs_tool.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "yb/util/logging.h"

#include "yb/common/schema.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/human_readable.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"

#include "yb/util/env.h"
#include "yb/util/status.h"

namespace yb {
namespace tools {

using log::ReadableLogSegment;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using tablet::Tablet;
using tablet::RaftGroupMetadata;
using tablet::RaftGroupMetadataPtr;

namespace {
string Indent(int indent) {
  return string(indent, ' ');
}

string IndentString(const string& s, int indent) {
  return Indent(indent) + StringReplace(s, "\n", "\n" + Indent(indent), true);
}
} // anonymous namespace

FsTool::FsTool(DetailLevel detail_level)
    : initialized_(false),
      detail_level_(detail_level) {
}

FsTool::~FsTool() {
}

Status FsTool::Init() {
  CHECK(!initialized_) << "Already initialized";
  // Allow read-only access to live blocks.
  FsManagerOpts opts;
  opts.read_only = true;
  // TODO(bogdan): do we use this tool? would we use it for more than tservers?
  opts.server_type = "tserver";
  fs_manager_.reset(new FsManager(Env::Default(), opts));
  RETURN_NOT_OK(fs_manager_->CheckAndOpenFileSystemRoots());

  LOG(INFO) << "Opened file system with uuid: " << fs_manager_->uuid();

  initialized_ = true;
  return Status::OK();
}

Status FsTool::FsTree() {
  DCHECK(initialized_);

  fs_manager_->DumpFileSystemTree(std::cout);
  return Status::OK();
}

Status FsTool::ListAllLogSegments() {
  DCHECK(initialized_);

  auto wal_root_dirs = fs_manager_->GetWalRootDirs();
  for (auto const& wals_dir : wal_root_dirs) {
    if (!fs_manager_->Exists(wals_dir)) {
      return STATUS(Corruption, Substitute(
          "root log directory '$0' does not exist", wals_dir));
    }

    std::cout << "Root log directory: " << wals_dir << std::endl;

    vector<string> children;
    vector<string> tables;
    RETURN_NOT_OK_PREPEND(fs_manager_->ListDir(wals_dir, &tables),
                          "Could not list table directories");
    auto has_dot_prefix = [](const std::string& s) {
                            return HasPrefixString(s, ".");
                          };
    tables.erase(std::remove_if(tables.begin(), tables.end(), has_dot_prefix), tables.end());
    for (const auto &table : tables) {
      auto table_wal_dir = JoinPathSegments(wals_dir, table);
      RETURN_NOT_OK_PREPEND(fs_manager_->ListDir(table_wal_dir, &children),
                            "Could not list log directories");
      for (const string &child : children) {
        if (has_dot_prefix(child)) {
          // Hidden files or ./..
          VLOG(1) << "Ignoring hidden file in root log directory " << child;
          continue;
        }
        string path = JoinPathSegments(table_wal_dir, child);
        if (HasSuffixString(child, FsManager::kWalsRecoveryDirSuffix)) {
          std::cout << "Log recovery dir found: " << path << std::endl;
        } else {
          std::cout << "Log directory: " << path << std::endl;
        }
        RETURN_NOT_OK(ListSegmentsInDir(path));
      }
    }
  }
  return Status::OK();
}

Status FsTool::ListLogSegmentsForTablet(const string& tablet_id) {
  DCHECK(initialized_);

  fs_manager_->LookupTablet(tablet_id);
  auto meta = VERIFY_RESULT(RaftGroupMetadata::Load(fs_manager_.get(), tablet_id));

  const string& tablet_wal_dir = meta->wal_dir();
  if (!fs_manager_->Exists(tablet_wal_dir)) {
    return STATUS(NotFound, Substitute("tablet '$0' has no logs in wals dir '$1'",
                                       tablet_id, tablet_wal_dir));
  }
  std::cout << "Tablet WAL dir found: " << tablet_wal_dir << std::endl;
  RETURN_NOT_OK(ListSegmentsInDir(tablet_wal_dir));
  const string recovery_dir = fs_manager_->GetTabletWalRecoveryDir(tablet_wal_dir);
  if (fs_manager_->Exists(recovery_dir)) {
    std::cout << "Recovery dir found: " << recovery_dir << std::endl;
    RETURN_NOT_OK(ListSegmentsInDir(recovery_dir));
  }
  return Status::OK();
}


Status FsTool::ListAllTablets() {
  DCHECK(initialized_);

  vector<string> tablets = VERIFY_RESULT(fs_manager_->ListTabletIds());
  for (const string& tablet : tablets) {
    if (detail_level_ >= HEADERS_ONLY) {
      std::cout << "Tablet: " << tablet << std::endl;
      RETURN_NOT_OK(PrintTabletMeta(tablet, 2));
    } else {
      std::cout << "\t" << tablet << std::endl;
    }
  }
  return Status::OK();
}

Status FsTool::ListSegmentsInDir(const string& segments_dir) {
  vector<string> segments;
  RETURN_NOT_OK_PREPEND(fs_manager_->ListDir(segments_dir, &segments),
                        "Unable to list log segments");
  std::cout << "Segments in " << segments_dir << ":" << std::endl;
  for (const string& segment : segments) {
    if (!log::IsLogFileName(segment)) {
      continue;
    }
    if (detail_level_ >= HEADERS_ONLY) {
      std::cout << "Segment: " << segment << std::endl;
      string path = JoinPathSegments(segments_dir, segment);
      RETURN_NOT_OK(PrintLogSegmentHeader(path, 2));
    } else {
      std::cout << "\t" << segment << std::endl;
    }
  }
  return Status::OK();
}

Status FsTool::PrintLogSegmentHeader(const string& path,
                                     int indent) {
  auto segment_result = ReadableLogSegment::Open(fs_manager_->env(), path);
  if (!segment_result.ok()) {
    auto s = segment_result.status();
    if (s.IsUninitialized()) {
      LOG(ERROR) << path << " is not initialized: " << s.ToString();
      return Status::OK();
    }
    if (s.IsCorruption()) {
      LOG(ERROR) << path << " is corrupt: " << s.ToString();
      return Status::OK();
    }
    return s.CloneAndPrepend("Unexpected error reading log segment " + path);
  }
  const auto& segment = *segment_result;

  std::cout << Indent(indent) << "Size: "
            << HumanReadableNumBytes::ToStringWithoutRounding(segment->file_size())
            << std::endl;
  std::cout << Indent(indent) << "Header: " << std::endl;
  std::cout << IndentString(segment->header().DebugString(), indent);
  return Status::OK();
}

Status FsTool::PrintTabletMeta(const string& tablet_id, int indent) {
  fs_manager_->LookupTablet(tablet_id);
  auto meta = VERIFY_RESULT(RaftGroupMetadata::Load(fs_manager_.get(), tablet_id));

  const SchemaPtr schema = meta->schema();

  std::cout << Indent(indent) << "Partition: "
            << meta->partition_schema()->PartitionDebugString(*meta->partition(), *schema)
            << std::endl;
  std::cout << Indent(indent) << "Table name: " << meta->table_name()
            << " Table id: " << meta->table_id() << std::endl;
  std::cout << Indent(indent) << "Schema (version=" << meta->schema_version() << "): "
            << schema->ToString() << std::endl;

  tablet::RaftGroupReplicaSuperBlockPB pb;
  meta->ToSuperBlock(&pb);
  std::cout << "Superblock:\n" << pb.DebugString() << std::endl;

  return Status::OK();
}

Status FsTool::DumpTabletData(const std::string& tablet_id) {
  DCHECK(initialized_);

  fs_manager_->LookupTablet(tablet_id);
  auto meta = VERIFY_RESULT(RaftGroupMetadata::Load(fs_manager_.get(), tablet_id));

  scoped_refptr<log::LogAnchorRegistry> reg(new log::LogAnchorRegistry());
  tablet::TabletOptions tablet_options;
  tablet::TabletInitData tablet_init_data = {
      .metadata = meta,
      .client_future = std::shared_future<client::YBClient*>(),
      .clock = scoped_refptr<server::Clock>(),
      .parent_mem_tracker = shared_ptr<MemTracker>(),
      .block_based_table_mem_tracker = shared_ptr<MemTracker>(),
      .metric_registry = nullptr,
      .log_anchor_registry = reg.get(),
      .tablet_options = tablet_options,
      .log_prefix_suffix = std::string(),
      .transaction_participant_context = nullptr,
      .local_tablet_filter = client::LocalTabletFilter(),
      .transaction_coordinator_context = nullptr,
      .txns_enabled = tablet::TransactionsEnabled::kTrue,
      .is_sys_catalog = tablet::IsSysCatalogTablet(tablet_id == master::kSysCatalogTabletId),
      .snapshot_coordinator = nullptr,
      .tablet_splitter = nullptr,
      .allowed_history_cutoff_provider = {},
      .transaction_manager_provider = nullptr,
      .full_compaction_pool = nullptr,
      .admin_triggered_compaction_pool = nullptr,
      .post_split_compaction_added = nullptr,
      .metadata_cache = nullptr
  };
  Tablet t(tablet_init_data);
  RETURN_NOT_OK_PREPEND(t.Open(), "Couldn't open tablet");
  vector<string> lines;
  RETURN_NOT_OK_PREPEND(t.DebugDump(&lines), "Couldn't dump tablet");
  for (const string& line : lines) {
    std::cout << line << std::endl;
  }
  return Status::OK();
}

Status FsTool::PrintUUID(int indent) {
  std::cout << Indent(indent) << fs_manager_->uuid() << std::endl;
  return Status::OK();
}

} // namespace tools
} // namespace yb
