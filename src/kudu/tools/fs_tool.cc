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

#include "kudu/tools/fs_tool.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include <boost/function.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "kudu/cfile/cfile_reader.h"
#include "kudu/common/rowblock.h"
#include "kudu/common/row_changelist.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/log_reader.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/strings/human_readable.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/tablet/deltafile.h"
#include "kudu/tablet/tablet.h"
#include "kudu/util/env.h"
#include "kudu/util/logging.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/status.h"

namespace kudu {
namespace tools {

using cfile::CFileIterator;
using cfile::CFileReader;
using cfile::DumpIterator;
using cfile::DumpIteratorOptions;
using cfile::ReaderOptions;
using fs::ReadableBlock;
using log::LogReader;
using log::ReadableLogSegment;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using tablet::CFileSet;
using tablet::DeltaFileReader;
using tablet::DeltaIterator;
using tablet::DeltaKeyAndUpdate;
using tablet::DeltaType;
using tablet::MvccSnapshot;
using tablet::RowSetMetadata;
using tablet::Tablet;
using tablet::TabletMetadata;

static const char* const kSeparatorLine =
  "----------------------------------------------------------------------\n";

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
  fs_manager_.reset(new FsManager(Env::Default(), opts));
  RETURN_NOT_OK(fs_manager_->Open());

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

  string wals_dir = fs_manager_->GetWalsRootDir();
  if (!fs_manager_->Exists(wals_dir)) {
    return Status::Corruption(Substitute(
        "root log directory '$0' does not exist", wals_dir));
  }

  std::cout << "Root log directory: " << wals_dir << std::endl;

  vector<string> children;
  RETURN_NOT_OK_PREPEND(fs_manager_->ListDir(wals_dir, &children),
                        "Could not list log directories");
  for (const string& child : children) {
    if (HasPrefixString(child, ".")) {
      // Hidden files or ./..
      VLOG(1) << "Ignoring hidden file in root log directory " << child;
      continue;
    }
    string path = JoinPathSegments(wals_dir, child);
    if (HasSuffixString(child, FsManager::kWalsRecoveryDirSuffix)) {
      std::cout << "Log recovery dir found: " << path << std::endl;
    } else {
      std::cout << "Log directory: " << path << std::endl;
    }
    RETURN_NOT_OK(ListSegmentsInDir(path));
  }
  return Status::OK();
}

Status FsTool::ListLogSegmentsForTablet(const string& tablet_id) {
  DCHECK(initialized_);

  string tablet_wal_dir = fs_manager_->GetTabletWalDir(tablet_id);
  if (!fs_manager_->Exists(tablet_wal_dir)) {
    return Status::NotFound(Substitute("tablet '$0' has no logs in wals dir '$1'",
                                       tablet_id, tablet_wal_dir));
  }
  std::cout << "Tablet WAL dir found: " << tablet_wal_dir << std::endl;
  RETURN_NOT_OK(ListSegmentsInDir(tablet_wal_dir));
  string recovery_dir = fs_manager_->GetTabletWalRecoveryDir(tablet_id);
  if (fs_manager_->Exists(recovery_dir)) {
    std::cout << "Recovery dir found: " << recovery_dir << std::endl;
    RETURN_NOT_OK(ListSegmentsInDir(recovery_dir));
  }
  return Status::OK();
}


Status FsTool::ListAllTablets() {
  DCHECK(initialized_);

  vector<string> tablets;
  RETURN_NOT_OK(fs_manager_->ListTabletIds(&tablets));
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
  scoped_refptr<ReadableLogSegment> segment;
  Status s = ReadableLogSegment::Open(fs_manager_->env(),
                                      path,
                                      &segment);

  if (s.IsUninitialized()) {
    LOG(ERROR) << path << " is not initialized: " << s.ToString();
    return Status::OK();
  }
  if (s.IsCorruption()) {
    LOG(ERROR) << path << " is corrupt: " << s.ToString();
    return Status::OK();
  }
  RETURN_NOT_OK_PREPEND(s, "Unexpected error reading log segment " + path);

  std::cout << Indent(indent) << "Size: "
            << HumanReadableNumBytes::ToStringWithoutRounding(segment->file_size())
            << std::endl;
  std::cout << Indent(indent) << "Header: " << std::endl;
  std::cout << IndentString(segment->header().DebugString(), indent);
  return Status::OK();
}

Status FsTool::PrintTabletMeta(const string& tablet_id, int indent) {
  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK(TabletMetadata::Load(fs_manager_.get(), tablet_id, &meta));

  const Schema& schema = meta->schema();

  std::cout << Indent(indent) << "Partition: "
            << meta->partition_schema().PartitionDebugString(meta->partition(), meta->schema())
            << std::endl;
  std::cout << Indent(indent) << "Table name: " << meta->table_name()
            << " Table id: " << meta->table_id() << std::endl;
  std::cout << Indent(indent) << "Schema (version=" << meta->schema_version() << "): "
            << schema.ToString() << std::endl;

  tablet::TabletSuperBlockPB pb;
  RETURN_NOT_OK_PREPEND(meta->ToSuperBlock(&pb), "Could not get superblock");
  std::cout << "Superblock:\n" << pb.DebugString() << std::endl;

  return Status::OK();
}

Status FsTool::ListBlocksForAllTablets() {
  DCHECK(initialized_);

  vector<string> tablets;
  RETURN_NOT_OK(fs_manager_->ListTabletIds(&tablets));
  for (string tablet : tablets) {
    RETURN_NOT_OK(ListBlocksForTablet(tablet));
  }
  return Status::OK();
}

Status FsTool::ListBlocksForTablet(const string& tablet_id) {
  DCHECK(initialized_);

  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK(TabletMetadata::Load(fs_manager_.get(), tablet_id, &meta));

  if (meta->rowsets().empty()) {
    std::cout << "No rowsets found on disk for tablet " << tablet_id << std::endl;
    return Status::OK();
  }

  std::cout << "Listing all data blocks in tablet " << tablet_id << ":" << std::endl;

  Schema schema = meta->schema();

  size_t idx = 0;
  for (const shared_ptr<RowSetMetadata>& rs_meta : meta->rowsets())  {
    std::cout << "Rowset " << idx++ << std::endl;
    RETURN_NOT_OK(ListBlocksInRowSet(schema, *rs_meta));
  }

  return Status::OK();
}

Status FsTool::ListBlocksInRowSet(const Schema& schema,
                                  const RowSetMetadata& rs_meta) {
  RowSetMetadata::ColumnIdToBlockIdMap col_blocks = rs_meta.GetColumnBlocksById();
  for (const RowSetMetadata::ColumnIdToBlockIdMap::value_type& e : col_blocks) {
    ColumnId col_id = e.first;
    const BlockId& block_id = e.second;
    std::cout << "Column block for column ID " << col_id;
    int col_idx = schema.find_column_by_id(col_id);
    if (col_idx != -1) {
      std::cout << " (" << schema.column(col_idx).ToString() << ")";
    }
    std::cout << ": ";
    std::cout << block_id.ToString() << std::endl;
  }

  for (const BlockId& block : rs_meta.undo_delta_blocks()) {
    std::cout << "UNDO: " << block.ToString() << std::endl;
  }

  for (const BlockId& block : rs_meta.redo_delta_blocks()) {
    std::cout << "REDO: " << block.ToString() << std::endl;
  }

  return Status::OK();
}

Status FsTool::DumpTabletBlocks(const std::string& tablet_id,
                                const DumpOptions& opts,
                                int indent) {
  DCHECK(initialized_);

  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK(TabletMetadata::Load(fs_manager_.get(), tablet_id, &meta));

  if (meta->rowsets().empty()) {
    std::cout << Indent(indent) << "No rowsets found on disk for tablet "
              << tablet_id << std::endl;
    return Status::OK();
  }

  Schema schema = meta->schema();

  size_t idx = 0;
  for (const shared_ptr<RowSetMetadata>& rs_meta : meta->rowsets())  {
    std::cout << std::endl << Indent(indent) << "Dumping rowset " << idx++
              << std::endl << Indent(indent) << kSeparatorLine;
    RETURN_NOT_OK(DumpRowSetInternal(meta->schema(), rs_meta, opts, indent + 2));
  }
  return Status::OK();
}

Status FsTool::DumpTabletData(const std::string& tablet_id) {
  DCHECK(initialized_);

  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK(TabletMetadata::Load(fs_manager_.get(), tablet_id, &meta));

  scoped_refptr<log::LogAnchorRegistry> reg(new log::LogAnchorRegistry());
  Tablet t(meta, scoped_refptr<server::Clock>(nullptr), shared_ptr<MemTracker>(),
           nullptr, reg.get());
  RETURN_NOT_OK_PREPEND(t.Open(), "Couldn't open tablet");
  vector<string> lines;
  RETURN_NOT_OK_PREPEND(t.DebugDump(&lines), "Couldn't dump tablet");
  for (const string& line : lines) {
    std::cout << line << std::endl;
  }
  return Status::OK();
}

Status FsTool::DumpRowSet(const string& tablet_id,
                          int64_t rowset_id,
                          const DumpOptions& opts,
                          int indent) {
  DCHECK(initialized_);

  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK(TabletMetadata::Load(fs_manager_.get(), tablet_id, &meta));

  for (const shared_ptr<RowSetMetadata>& rs_meta : meta->rowsets())  {
    if (rs_meta->id() == rowset_id) {
      return DumpRowSetInternal(meta->schema(), rs_meta, opts, indent);
    }
  }

  return Status::InvalidArgument(
      Substitute("Could not find rowset $0 in tablet id $1", rowset_id, tablet_id));
}

Status FsTool::DumpRowSetInternal(const Schema& schema,
                                  const shared_ptr<RowSetMetadata>& rs_meta,
                                  const DumpOptions& opts,
                                  int indent) {
  tablet::RowSetDataPB pb;
  rs_meta->ToProtobuf(&pb);

  std::cout << Indent(indent) << "RowSet metadata: " << pb.DebugString() << std::endl
            << std::endl;

  RowSetMetadata::ColumnIdToBlockIdMap col_blocks = rs_meta->GetColumnBlocksById();
  for (const RowSetMetadata::ColumnIdToBlockIdMap::value_type& e : col_blocks) {
    ColumnId col_id = e.first;
    const BlockId& block_id = e.second;

    std::cout << Indent(indent) << "Dumping column block " << block_id << " for column id "
              << col_id;
    int col_idx = schema.find_column_by_id(col_id);
    if (col_idx != -1) {
      std::cout << "( " << schema.column(col_idx).ToString() <<  ")";
    }
    std::cout << ":" << std::endl;
    std::cout << Indent(indent) << kSeparatorLine;
    if (opts.metadata_only) continue;
    RETURN_NOT_OK(DumpCFileBlockInternal(block_id, opts, indent));
    std::cout << std::endl;
  }

  for (const BlockId& block : rs_meta->undo_delta_blocks()) {
    std::cout << Indent(indent) << "Dumping undo delta block " << block << ":" << std::endl
              << Indent(indent) << kSeparatorLine;
    RETURN_NOT_OK(DumpDeltaCFileBlockInternal(schema,
                                              rs_meta,
                                              block,
                                              tablet::UNDO,
                                              opts,
                                              indent,
                                              opts.metadata_only));
    std::cout << std::endl;
  }

  for (const BlockId& block : rs_meta->redo_delta_blocks()) {
    std::cout << Indent(indent) << "Dumping redo delta block " << block << ":" << std::endl
              << Indent(indent) << kSeparatorLine;
    RETURN_NOT_OK(DumpDeltaCFileBlockInternal(schema,
                                              rs_meta,
                                              block,
                                              tablet::REDO,
                                              opts,
                                              indent,
                                              opts.metadata_only));
    std::cout << std::endl;
  }

  return Status::OK();
}

Status FsTool::DumpCFileBlock(const std::string& block_id_str,
                              const DumpOptions &opts,
                              int indent) {
  uint64_t numeric_id;
  if (!safe_strtou64(block_id_str, &numeric_id) &&
      !safe_strtou64_base(block_id_str, &numeric_id, 16)) {
    return Status::InvalidArgument(Substitute("block '$0' could not be parsed",
                                              block_id_str));
  }
  BlockId block_id(numeric_id);
  if (!fs_manager_->BlockExists(block_id)) {
    return Status::NotFound(Substitute("block '$0' does not exist", block_id_str));
  }
  return DumpCFileBlockInternal(block_id, opts, indent);
}

Status FsTool::PrintUUID(int indent) {
  std::cout << Indent(indent) << fs_manager_->uuid() << std::endl;
  return Status::OK();
}

Status FsTool::DumpCFileBlockInternal(const BlockId& block_id,
                                      const DumpOptions& opts,
                                      int indent) {
  gscoped_ptr<ReadableBlock> block;
  RETURN_NOT_OK(fs_manager_->OpenBlock(block_id, &block));
  gscoped_ptr<CFileReader> reader;
  RETURN_NOT_OK(CFileReader::Open(block.Pass(), ReaderOptions(), &reader));

  std::cout << Indent(indent) << "CFile Header: "
            << reader->header().ShortDebugString() << std::endl;
  std::cout << Indent(indent) << reader->footer().num_values()
            << " values:" << std::endl;

  gscoped_ptr<CFileIterator> it;
  RETURN_NOT_OK(reader->NewIterator(&it, CFileReader::DONT_CACHE_BLOCK));
  RETURN_NOT_OK(it->SeekToFirst());
  DumpIteratorOptions iter_opts;
  iter_opts.nrows = opts.nrows;
  iter_opts.print_rows = detail_level_ > HEADERS_ONLY;
  return DumpIterator(*reader, it.get(), &std::cout, iter_opts, indent + 2);
}

Status FsTool::DumpDeltaCFileBlockInternal(const Schema& schema,
                                           const shared_ptr<RowSetMetadata>& rs_meta,
                                           const BlockId& block_id,
                                           DeltaType delta_type,
                                           const DumpOptions& opts,
                                           int indent,
                                           bool metadata_only) {
  // Open the delta reader
  gscoped_ptr<ReadableBlock> readable_block;
  RETURN_NOT_OK(fs_manager_->OpenBlock(block_id, &readable_block));
  shared_ptr<DeltaFileReader> delta_reader;
  RETURN_NOT_OK(DeltaFileReader::Open(readable_block.Pass(),
                                      block_id,
                                      &delta_reader,
                                      delta_type));

  std::cout << Indent(indent) << "Delta stats: " << delta_reader->delta_stats().ToString()
      << std::endl;
  if (metadata_only) {
    return Status::OK();
  }

  // Create the delta iterator.
  // TODO: see if it's worth re-factoring NewDeltaIterator to return a
  // gscoped_ptr that can then be released if we need a raw or shared
  // pointer.
  DeltaIterator* raw_iter;

  MvccSnapshot snap_all;
  if (delta_type == tablet::REDO) {
    snap_all = MvccSnapshot::CreateSnapshotIncludingAllTransactions();
  } else if (delta_type == tablet::UNDO) {
    snap_all = MvccSnapshot::CreateSnapshotIncludingNoTransactions();
  }

  Status s = delta_reader->NewDeltaIterator(&schema, snap_all, &raw_iter);

  if (s.IsNotFound()) {
    std::cout << "Empty delta block." << std::endl;
    return Status::OK();
  }
  RETURN_NOT_OK(s);

  // NewDeltaIterator returns Status::OK() iff a new DeltaIterator is created. Thus,
  // it's safe to have a gscoped_ptr take possesion of 'raw_iter' here.
  gscoped_ptr<DeltaIterator> delta_iter(raw_iter);
  RETURN_NOT_OK(delta_iter->Init(NULL));
  RETURN_NOT_OK(delta_iter->SeekToOrdinal(0));

  // TODO: it's awkward that whenever we want to iterate over deltas we also
  // need to open the CFileSet for the rowset. Ideally, we should use information stored
  // in the footer/store additional information in the footer as to make it feasible
  // iterate over all deltas using a DeltaFileIterator alone.
  shared_ptr<CFileSet> cfileset(new CFileSet(rs_meta));
  RETURN_NOT_OK(cfileset->Open());
  gscoped_ptr<CFileSet::Iterator> cfileset_iter(cfileset->NewIterator(&schema));

  RETURN_NOT_OK(cfileset_iter->Init(NULL));

  const size_t kRowsPerBlock  = 100;
  size_t nrows = 0;
  size_t ndeltas = 0;
  Arena arena(32 * 1024, 128 * 1024);
  RowBlock block(schema, kRowsPerBlock, &arena);

  // See tablet/delta_compaction.cc to understand why this loop is structured the way
  // it is.
  while (cfileset_iter->HasNext()) {
    size_t n;
    if (opts.nrows > 0) {
      // Note: number of deltas may not equal the number of rows, but
      // since this is a CLI tool (and the nrows option exists
      // primarily to limit copious output) it's okay not to be
      // exact here.
      size_t remaining = opts.nrows - nrows;
      if (remaining == 0) break;
      n = std::min(remaining, kRowsPerBlock);
    } else {
      n = kRowsPerBlock;
    }

    arena.Reset();
    cfileset_iter->PrepareBatch(&n);

    block.Resize(n);

    RETURN_NOT_OK(delta_iter->PrepareBatch(n, DeltaIterator::PREPARE_FOR_COLLECT));
    vector<DeltaKeyAndUpdate> out;
    RETURN_NOT_OK(delta_iter->FilterColumnIdsAndCollectDeltas(vector<ColumnId>(),
                                                              &out,
                                                              &arena));
    for (const DeltaKeyAndUpdate& upd : out) {
      if (detail_level_ > HEADERS_ONLY) {
        std::cout << Indent(indent) << upd.key.ToString() << " "
                  << RowChangeList(upd.cell).ToString(schema) << std::endl;
        ++ndeltas;
      }
    }
    RETURN_NOT_OK(cfileset_iter->FinishBatch());

    nrows += n;
  }

  VLOG(1) << "Processed " << ndeltas << " deltas, for total of " << nrows << " possible rows.";
  return Status::OK();
}

} // namespace tools
} // namespace kudu
