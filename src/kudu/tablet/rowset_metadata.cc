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

#include "kudu/tablet/rowset_metadata.h"

#include <string>
#include <utility>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/map-util.h"

using strings::Substitute;

namespace kudu {
namespace tablet {

// ============================================================================
//  RowSet Metadata
// ============================================================================
Status RowSetMetadata::Load(TabletMetadata* tablet_metadata,
                            const RowSetDataPB& pb,
                            gscoped_ptr<RowSetMetadata>* metadata) {
  gscoped_ptr<RowSetMetadata> ret(new RowSetMetadata(tablet_metadata));
  RETURN_NOT_OK(ret->InitFromPB(pb));
  metadata->reset(ret.release());
  return Status::OK();
}

Status RowSetMetadata::CreateNew(TabletMetadata* tablet_metadata,
                                 int64_t id,
                                 gscoped_ptr<RowSetMetadata>* metadata) {
  metadata->reset(new RowSetMetadata(tablet_metadata, id));
  return Status::OK();
}

Status RowSetMetadata::Flush() {
  return tablet_metadata_->Flush();
}

Status RowSetMetadata::InitFromPB(const RowSetDataPB& pb) {
  CHECK(!initted_);

  id_ = pb.id();

  // Load Bloom File
  if (pb.has_bloom_block()) {
    bloom_block_ = BlockId::FromPB(pb.bloom_block());
  }

  // Load AdHoc Index File
  if (pb.has_adhoc_index_block()) {
    adhoc_index_block_ = BlockId::FromPB(pb.adhoc_index_block());
  }

  // Load Column Files
  for (const ColumnDataPB& col_pb : pb.columns()) {
    ColumnId col_id = ColumnId(col_pb.column_id());
    blocks_by_col_id_[col_id] = BlockId::FromPB(col_pb.block());
  }

  // Load redo delta files
  for (const DeltaDataPB& redo_delta_pb : pb.redo_deltas()) {
    redo_delta_blocks_.push_back(BlockId::FromPB(redo_delta_pb.block()));
  }

  last_durable_redo_dms_id_ = pb.last_durable_dms_id();

  // Load undo delta files
  for (const DeltaDataPB& undo_delta_pb : pb.undo_deltas()) {
    undo_delta_blocks_.push_back(BlockId::FromPB(undo_delta_pb.block()));
  }

  initted_ = true;
  return Status::OK();
}

void RowSetMetadata::ToProtobuf(RowSetDataPB *pb) {
  pb->set_id(id_);

  lock_guard<LockType> l(&lock_);

  // Write Column Files
  for (const ColumnIdToBlockIdMap::value_type& e : blocks_by_col_id_) {
    ColumnId col_id = e.first;
    const BlockId& block_id = e.second;

    ColumnDataPB *col_data = pb->add_columns();
    block_id.CopyToPB(col_data->mutable_block());
    col_data->set_column_id(col_id);
  }

  // Write Delta Files
  pb->set_last_durable_dms_id(last_durable_redo_dms_id_);

  for (const BlockId& redo_delta_block : redo_delta_blocks_) {
    DeltaDataPB *redo_delta_pb = pb->add_redo_deltas();
    redo_delta_block.CopyToPB(redo_delta_pb->mutable_block());
  }

  for (const BlockId& undo_delta_block : undo_delta_blocks_) {
    DeltaDataPB *undo_delta_pb = pb->add_undo_deltas();
    undo_delta_block.CopyToPB(undo_delta_pb->mutable_block());
  }

  // Write Bloom File
  if (!bloom_block_.IsNull()) {
    bloom_block_.CopyToPB(pb->mutable_bloom_block());
  }

  // Write AdHoc Index
  if (!adhoc_index_block_.IsNull()) {
    adhoc_index_block_.CopyToPB(pb->mutable_adhoc_index_block());
  }
}

const string RowSetMetadata::ToString() const {
  return Substitute("RowSet($0)", id_);
}

void RowSetMetadata::SetColumnDataBlocks(const ColumnIdToBlockIdMap& blocks) {
  lock_guard<LockType> l(&lock_);
  blocks_by_col_id_ = blocks;
}

Status RowSetMetadata::CommitRedoDeltaDataBlock(int64_t dms_id,
                                                const BlockId& block_id) {
  lock_guard<LockType> l(&lock_);
  last_durable_redo_dms_id_ = dms_id;
  redo_delta_blocks_.push_back(block_id);
  return Status::OK();
}

Status RowSetMetadata::CommitUndoDeltaDataBlock(const BlockId& block_id) {
  lock_guard<LockType> l(&lock_);
  undo_delta_blocks_.push_back(block_id);
  return Status::OK();
}

Status RowSetMetadata::CommitUpdate(const RowSetMetadataUpdate& update) {
  vector<BlockId> removed;
  {
    lock_guard<LockType> l(&lock_);

    for (const RowSetMetadataUpdate::ReplaceDeltaBlocks rep :
                  update.replace_redo_blocks_) {
      CHECK(!rep.to_remove.empty());

      auto start_it = std::find(redo_delta_blocks_.begin(),
                                redo_delta_blocks_.end(), rep.to_remove[0]);

      auto end_it = start_it;
      for (const BlockId& b : rep.to_remove) {
        if (end_it == redo_delta_blocks_.end() || *end_it != b) {
          return Status::InvalidArgument(
              Substitute("Cannot find subsequence <$0> in <$1>",
                         BlockId::JoinStrings(rep.to_remove),
                         BlockId::JoinStrings(redo_delta_blocks_)));
        }
        ++end_it;
      }

      removed.insert(removed.end(), start_it, end_it);
      redo_delta_blocks_.erase(start_it, end_it);
      redo_delta_blocks_.insert(start_it, rep.to_add.begin(), rep.to_add.end());
    }

    // Add new redo blocks
    for (const BlockId& b : update.new_redo_blocks_) {
      redo_delta_blocks_.push_back(b);
    }

    if (!update.new_undo_block_.IsNull()) {
      // Front-loading to keep the UNDO files in their natural order.
      undo_delta_blocks_.insert(undo_delta_blocks_.begin(), update.new_undo_block_);
    }

    for (const ColumnIdToBlockIdMap::value_type& e : update.cols_to_replace_) {
      // If we are major-compacting deltas into a column which previously had no
      // base-data (e.g. because it was newly added), then there will be no original
      // block there to replace.
      BlockId old_block_id;
      if (UpdateReturnCopy(&blocks_by_col_id_, e.first, e.second, &old_block_id)) {
        removed.push_back(old_block_id);
      }
    }

    for (ColumnId col_id : update.col_ids_to_remove_) {
      BlockId old = FindOrDie(blocks_by_col_id_, col_id);
      CHECK_EQ(1, blocks_by_col_id_.erase(col_id));
      removed.push_back(old);
    }
  }

  // Should only be NULL in tests.
  if (tablet_metadata()) {
    tablet_metadata()->AddOrphanedBlocks(removed);
  }
  return Status::OK();
}

vector<BlockId> RowSetMetadata::GetAllBlocks() {
  vector<BlockId> blocks;
  lock_guard<LockType> l(&lock_);
  if (!adhoc_index_block_.IsNull()) {
    blocks.push_back(adhoc_index_block_);
  }
  if (!bloom_block_.IsNull()) {
    blocks.push_back(bloom_block_);
  }
  AppendValuesFromMap(blocks_by_col_id_, &blocks);

  blocks.insert(blocks.end(),
                undo_delta_blocks_.begin(), undo_delta_blocks_.end());
  blocks.insert(blocks.end(),
                redo_delta_blocks_.begin(), redo_delta_blocks_.end());
  return blocks;
}

RowSetMetadataUpdate::RowSetMetadataUpdate() {
}

RowSetMetadataUpdate::~RowSetMetadataUpdate() {
}

RowSetMetadataUpdate& RowSetMetadataUpdate::ReplaceColumnId(ColumnId col_id,
                                                            const BlockId& block_id) {
  InsertOrDie(&cols_to_replace_, col_id, block_id);
  return *this;
}

RowSetMetadataUpdate& RowSetMetadataUpdate::RemoveColumnId(ColumnId col_id) {
  col_ids_to_remove_.push_back(col_id);
  return *this;
}

RowSetMetadataUpdate& RowSetMetadataUpdate::ReplaceRedoDeltaBlocks(
    const std::vector<BlockId>& to_remove,
    const std::vector<BlockId>& to_add) {

  ReplaceDeltaBlocks rdb = { to_remove, to_add };
  replace_redo_blocks_.push_back(rdb);
  return *this;
}

RowSetMetadataUpdate& RowSetMetadataUpdate::SetNewUndoBlock(const BlockId& undo_block) {
  new_undo_block_ = undo_block;
  return *this;
}

} // namespace tablet
} // namespace kudu
