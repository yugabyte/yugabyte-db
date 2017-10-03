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

#include "kudu/tablet/deltafile.h"

#include <arpa/inet.h>
#include <memory>
#include <string>

#include "kudu/common/wire_protocol.h"
#include "kudu/cfile/binary_plain_block.h"
#include "kudu/cfile/block_encodings.h"
#include "kudu/cfile/block_handle.h"
#include "kudu/cfile/cfile_reader.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/tablet/mutation.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/util/coding-inl.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/pb_util.h"

DECLARE_bool(cfile_lazy_open);
DEFINE_int32(deltafile_default_block_size, 32*1024,
            "Block size for delta files. In the future, this may become configurable "
             "on a per-table basis.");
TAG_FLAG(deltafile_default_block_size, experimental);

using std::shared_ptr;

namespace kudu {

using cfile::BlockHandle;
using cfile::BlockPointer;
using cfile::IndexTreeIterator;
using cfile::BinaryPlainBlockDecoder;
using cfile::CFileReader;
using fs::ReadableBlock;
using fs::ScopedWritableBlockCloser;
using fs::WritableBlock;

namespace tablet {

const char * const DeltaFileReader::kDeltaStatsEntryName = "deltafilestats";

namespace {

} // namespace

DeltaFileWriter::DeltaFileWriter(gscoped_ptr<WritableBlock> block)
#ifndef NDEBUG
 : has_appended_(false)
#endif
{ // NOLINT(*)
  cfile::WriterOptions opts;
  opts.write_validx = true;
  opts.storage_attributes.cfile_block_size = FLAGS_deltafile_default_block_size;
  opts.storage_attributes.encoding = PLAIN_ENCODING;
  writer_.reset(new cfile::CFileWriter(opts, GetTypeInfo(BINARY), false, block.Pass()));
}


Status DeltaFileWriter::Start() {
  return writer_->Start();
}

Status DeltaFileWriter::Finish() {
  ScopedWritableBlockCloser closer;
  RETURN_NOT_OK(FinishAndReleaseBlock(&closer));
  return closer.CloseBlocks();
}

Status DeltaFileWriter::FinishAndReleaseBlock(ScopedWritableBlockCloser* closer) {
  return writer_->FinishAndReleaseBlock(closer);
}

Status DeltaFileWriter::DoAppendDelta(const DeltaKey &key,
                                      const RowChangeList &delta) {
  Slice delta_slice(delta.slice());

  // See TODO in RowChangeListEncoder::SetToReinsert
  CHECK(!delta.is_reinsert())
    << "TODO: REINSERT deltas cannot currently be written to disk "
    << "since they don't have a standalone encoded form.";

  tmp_buf_.clear();

  // Write the encoded form of the key to the file.
  key.EncodeTo(&tmp_buf_);

  tmp_buf_.append(delta_slice.data(), delta_slice.size());
  Slice tmp_buf_slice(tmp_buf_);

  return writer_->AppendEntries(&tmp_buf_slice, 1);
}

template<>
Status DeltaFileWriter::AppendDelta<REDO>(
  const DeltaKey &key, const RowChangeList &delta) {

#ifndef NDEBUG
  // Sanity check insertion order in debug mode.
  if (has_appended_) {
    DCHECK(last_key_.CompareTo<REDO>(key) <= 0)
      << "must insert redo deltas in sorted order (ascending key, then ascending ts): "
      << "got key " << key.ToString() << " after "
      << last_key_.ToString();
  }
  has_appended_ = true;
  last_key_ = key;
#endif

  return DoAppendDelta(key, delta);
}

template<>
Status DeltaFileWriter::AppendDelta<UNDO>(
  const DeltaKey &key, const RowChangeList &delta) {

#ifndef NDEBUG
  // Sanity check insertion order in debug mode.
  if (has_appended_) {
    DCHECK(last_key_.CompareTo<UNDO>(key) <= 0)
      << "must insert undo deltas in sorted order (ascending key, then descending ts): "
      << "got key " << key.ToString() << " after "
      << last_key_.ToString();
  }
  has_appended_ = true;
  last_key_ = key;
#endif

  return DoAppendDelta(key, delta);
}

Status DeltaFileWriter::WriteDeltaStats(const DeltaStats& stats) {
  DeltaStatsPB delta_stats_pb;
  stats.ToPB(&delta_stats_pb);

  faststring buf;
  if (!pb_util::SerializeToString(delta_stats_pb, &buf)) {
    return Status::IOError("Unable to serialize DeltaStatsPB", delta_stats_pb.DebugString());
  }

  writer_->AddMetadataPair(DeltaFileReader::kDeltaStatsEntryName, buf.ToString());
  return Status::OK();
}


////////////////////////////////////////////////////////////
// Reader
////////////////////////////////////////////////////////////

Status DeltaFileReader::Open(gscoped_ptr<ReadableBlock> block,
                             const BlockId& block_id,
                             shared_ptr<DeltaFileReader>* reader_out,
                             DeltaType delta_type) {
  shared_ptr<DeltaFileReader> df_reader;
  RETURN_NOT_OK(DeltaFileReader::OpenNoInit(block.Pass(),
                                            block_id, &df_reader, delta_type));
  RETURN_NOT_OK(df_reader->Init());

  *reader_out = df_reader;
  return Status::OK();
}

Status DeltaFileReader::OpenNoInit(gscoped_ptr<ReadableBlock> block,
                                   const BlockId& block_id,
                                   shared_ptr<DeltaFileReader>* reader_out,
                                   DeltaType delta_type) {
  gscoped_ptr<CFileReader> cf_reader;
  RETURN_NOT_OK(CFileReader::OpenNoInit(block.Pass(),
                                        cfile::ReaderOptions(), &cf_reader));
  gscoped_ptr<DeltaFileReader> df_reader(new DeltaFileReader(block_id,
                                                             cf_reader.release(),
                                                             delta_type));
  if (!FLAGS_cfile_lazy_open) {
    RETURN_NOT_OK(df_reader->Init());
  }

  reader_out->reset(df_reader.release());

  return Status::OK();
}

DeltaFileReader::DeltaFileReader(BlockId block_id, CFileReader *cf_reader,
                                 DeltaType delta_type)
    : reader_(cf_reader),
      block_id_(std::move(block_id)),
      delta_type_(delta_type) {}

Status DeltaFileReader::Init() {
  return init_once_.Init(&DeltaFileReader::InitOnce, this);
}

Status DeltaFileReader::InitOnce() {
  // Fully open the CFileReader if it was lazily opened earlier.
  //
  // If it's already initialized, this is a no-op.
  RETURN_NOT_OK(reader_->Init());

  if (!reader_->has_validx()) {
    return Status::Corruption("file does not have a value index!");
  }

  // Initialize delta file stats
  RETURN_NOT_OK(ReadDeltaStats());
  return Status::OK();
}

Status DeltaFileReader::ReadDeltaStats() {
  string filestats_pb_buf;
  if (!reader_->GetMetadataEntry(kDeltaStatsEntryName, &filestats_pb_buf)) {
    return Status::Corruption("missing delta stats from the delta file metadata");
  }

  DeltaStatsPB deltastats_pb;
  if (!deltastats_pb.ParseFromString(filestats_pb_buf)) {
    return Status::Corruption("unable to parse the delta stats protobuf");
  }
  gscoped_ptr<DeltaStats>stats(new DeltaStats());
  RETURN_NOT_OK(stats->InitFromPB(deltastats_pb));
  delta_stats_.swap(stats);
  return Status::OK();
}

bool DeltaFileReader::IsRelevantForSnapshot(const MvccSnapshot& snap) const {
  if (!init_once_.initted()) {
    // If we're not initted, it means we have no delta stats and must
    // assume that this file is relevant for every snapshot.
    return true;
  }
  if (delta_type_ == REDO) {
    return snap.MayHaveCommittedTransactionsAtOrAfter(delta_stats_->min_timestamp());
  }
  if (delta_type_ == UNDO) {
    return snap.MayHaveUncommittedTransactionsAtOrBefore(delta_stats_->max_timestamp());
  }
  LOG(DFATAL) << "Cannot reach here";
  return false;
}

Status DeltaFileReader::NewDeltaIterator(const Schema *projection,
                                         const MvccSnapshot &snap,
                                         DeltaIterator** iterator) const {
  if (IsRelevantForSnapshot(snap)) {
    if (VLOG_IS_ON(2)) {
      if (!init_once_.initted()) {
        VLOG(2) << (delta_type_ == REDO ? "REDO" : "UNDO") << " delta " << ToString()
                << "has no delta stats"
                << ": can't cull for " << snap.ToString();
      } else if (delta_type_ == REDO) {
        VLOG(2) << "REDO delta " << ToString()
                << " has min ts " << delta_stats_->min_timestamp().ToString()
                << ": can't cull for " << snap.ToString();
      } else {
        VLOG(2) << "UNDO delta " << ToString()
                << " has max ts " << delta_stats_->max_timestamp().ToString()
                << ": can't cull for " << snap.ToString();
      }
    }

    // Ugly cast, but it lets the iterator fully initialize the reader
    // during its first seek.
    *iterator = new DeltaFileIterator(
        const_cast<DeltaFileReader*>(this)->shared_from_this(), projection, snap, delta_type_);
    return Status::OK();
  } else {
    VLOG(2) << "Culling "
            << ((delta_type_ == REDO) ? "REDO":"UNDO")
            << " delta " << ToString() << " for " << snap.ToString();
    return Status::NotFound("MvccSnapshot outside the range of this delta.");
  }
}

Status DeltaFileReader::CheckRowDeleted(rowid_t row_idx, bool *deleted) const {
  MvccSnapshot snap_all(MvccSnapshot::CreateSnapshotIncludingAllTransactions());

  // TODO: would be nice to avoid allocation here, but we don't want to
  // duplicate all the logic from NewDeltaIterator. So, we'll heap-allocate
  // for now.
  Schema empty_schema;
  DeltaIterator* raw_iter;
  Status s = NewDeltaIterator(&empty_schema, snap_all, &raw_iter);
  if (s.IsNotFound()) {
    *deleted = false;
    return Status::OK();
  }
  RETURN_NOT_OK(s);

  gscoped_ptr<DeltaIterator> iter(raw_iter);

  ScanSpec spec;
  RETURN_NOT_OK(iter->Init(&spec));
  RETURN_NOT_OK(iter->SeekToOrdinal(row_idx));
  RETURN_NOT_OK(iter->PrepareBatch(1, DeltaIterator::PREPARE_FOR_APPLY));

  // TODO: this does an allocation - can we stack-allocate the bitmap
  // and make SelectionVector able to "release" its buffer?
  SelectionVector sel_vec(1);
  sel_vec.SetAllTrue();
  RETURN_NOT_OK(iter->ApplyDeletes(&sel_vec));
  *deleted = !sel_vec.IsRowSelected(0);
  return Status::OK();
}

uint64_t DeltaFileReader::EstimateSize() const {
  return reader_->file_size();
}


////////////////////////////////////////////////////////////
// DeltaFileIterator
////////////////////////////////////////////////////////////

DeltaFileIterator::DeltaFileIterator(shared_ptr<DeltaFileReader> dfr,
                                     const Schema *projection,
                                     MvccSnapshot snap, DeltaType delta_type)
    : dfr_(std::move(dfr)),
      projection_(projection),
      mvcc_snap_(std::move(snap)),
      prepared_idx_(0xdeadbeef),
      prepared_count_(0),
      prepared_(false),
      exhausted_(false),
      initted_(false),
      delta_type_(delta_type),
      cache_blocks_(CFileReader::CACHE_BLOCK) {}

Status DeltaFileIterator::Init(ScanSpec *spec) {
  DCHECK(!initted_) << "Already initted";

  if (spec) {
    cache_blocks_ = spec->cache_blocks() ? CFileReader::CACHE_BLOCK :
                                           CFileReader::DONT_CACHE_BLOCK;
  }

  initted_ = true;
  return Status::OK();
}

Status DeltaFileIterator::SeekToOrdinal(rowid_t idx) {
  DCHECK(initted_) << "Must call Init()";

  // Finish the initialization of any lazily-initialized state.
  RETURN_NOT_OK(dfr_->Init());
  if (!index_iter_) {
    index_iter_.reset(IndexTreeIterator::Create(
        dfr_->cfile_reader().get(),
        dfr_->cfile_reader()->validx_root()));
  }

  tmp_buf_.clear();
  DeltaKey(idx, Timestamp(0)).EncodeTo(&tmp_buf_);
  Slice key_slice(tmp_buf_);

  Status s = index_iter_->SeekAtOrBefore(key_slice);
  if (PREDICT_FALSE(s.IsNotFound())) {
    // Seeking to a value before the first value in the file
    // will return NotFound, due to the way the index seek
    // works. We need to special-case this and have the
    // iterator seek all the way down its leftmost branches
    // to get the correct result.
    s = index_iter_->SeekToFirst();
  }
  RETURN_NOT_OK(s);

  prepared_idx_ = idx;
  prepared_count_ = 0;
  prepared_ = false;
  delta_blocks_.clear();
  exhausted_ = false;
  return Status::OK();
}

Status DeltaFileIterator::ReadCurrentBlockOntoQueue() {
  DCHECK(initted_) << "Must call Init()";
  DCHECK(index_iter_) << "Must call SeekToOrdinal()";

  gscoped_ptr<PreparedDeltaBlock> pdb(new PreparedDeltaBlock());
  BlockPointer dblk_ptr = index_iter_->GetCurrentBlockPointer();
  RETURN_NOT_OK(dfr_->cfile_reader()->ReadBlock(
      dblk_ptr, cache_blocks_, &pdb->block_));

  // The data has been successfully read. Finish creating the decoder.
  pdb->prepared_block_start_idx_ = 0;
  pdb->block_ptr_ = dblk_ptr;

  // Decode the block.
  pdb->decoder_.reset(new BinaryPlainBlockDecoder(pdb->block_.data()));
  RETURN_NOT_OK(pdb->decoder_->ParseHeader());

  RETURN_NOT_OK(GetFirstRowIndexInCurrentBlock(&pdb->first_updated_idx_));
  RETURN_NOT_OK(GetLastRowIndexInDecodedBlock(*pdb->decoder_, &pdb->last_updated_idx_));

  #ifndef NDEBUG
  VLOG(2) << "Read delta block which updates " <<
    pdb->first_updated_idx_ << " through " <<
    pdb->last_updated_idx_;
  #endif

  delta_blocks_.push_back(pdb.release());
  return Status::OK();
}

Status DeltaFileIterator::GetFirstRowIndexInCurrentBlock(rowid_t *idx) {
  DCHECK(index_iter_) << "Must call SeekToOrdinal()";

  Slice index_entry = index_iter_->GetCurrentKey();
  DeltaKey k;
  RETURN_NOT_OK(k.DecodeFrom(&index_entry));
  *idx = k.row_idx();
  return Status::OK();
}

Status DeltaFileIterator::GetLastRowIndexInDecodedBlock(const BinaryPlainBlockDecoder &dec,
                                                        rowid_t *idx) {
  DCHECK_GT(dec.Count(), 0);
  Slice s(dec.string_at_index(dec.Count() - 1));
  DeltaKey k;
  RETURN_NOT_OK(k.DecodeFrom(&s));
  *idx = k.row_idx();
  return Status::OK();
}


string DeltaFileIterator::PreparedDeltaBlock::ToString() const {
  return StringPrintf("%d-%d (%s)", first_updated_idx_, last_updated_idx_,
                      block_ptr_.ToString().c_str());
}

Status DeltaFileIterator::PrepareBatch(size_t nrows, PrepareFlag flag) {
  DCHECK(initted_) << "Must call Init()";
  DCHECK(index_iter_) << "Must call SeekToOrdinal()";

  CHECK_GT(nrows, 0);

  rowid_t start_row = prepared_idx_ + prepared_count_;
  rowid_t stop_row = start_row + nrows - 1;

  // Remove blocks from our list which are no longer relevant to the range
  // being prepared.
  while (!delta_blocks_.empty() &&
         delta_blocks_.front().last_updated_idx_ < start_row) {
    delta_blocks_.pop_front();
  }

  while (!exhausted_) {
    rowid_t next_block_rowidx;
    RETURN_NOT_OK(GetFirstRowIndexInCurrentBlock(&next_block_rowidx));
    VLOG(2) << "Current delta block starting at row " << next_block_rowidx;

    if (next_block_rowidx > stop_row) {
      break;
    }

    RETURN_NOT_OK(ReadCurrentBlockOntoQueue());

    Status s = index_iter_->Next();
    if (s.IsNotFound()) {
      exhausted_ = true;
      break;
    }
    RETURN_NOT_OK(s);
  }

  if (!delta_blocks_.empty()) {
    PreparedDeltaBlock &block = delta_blocks_.front();
    int i = 0;
    for (i = block.prepared_block_start_idx_;
         i < block.decoder_->Count();
         i++) {
      Slice s(block.decoder_->string_at_index(i));
      DeltaKey key;
      RETURN_NOT_OK(key.DecodeFrom(&s));
      if (key.row_idx() >= start_row) break;
    }
    block.prepared_block_start_idx_ = i;
  }

  #ifndef NDEBUG
  VLOG(2) << "Done preparing deltas for " << start_row << "-" << stop_row
          << ": row block spans " << delta_blocks_.size() << " delta blocks";
  #endif
  prepared_idx_ = start_row;
  prepared_count_ = nrows;
  prepared_ = true;
  return Status::OK();
}

template<class Visitor>
Status DeltaFileIterator::VisitMutations(Visitor *visitor) {
  DCHECK(prepared_) << "must Prepare";

  rowid_t start_row = prepared_idx_;

  for (PreparedDeltaBlock &block : delta_blocks_) {
    BinaryPlainBlockDecoder &bpd = *block.decoder_;
    DVLOG(2) << "Visiting delta block " << block.first_updated_idx_ << "-"
      << block.last_updated_idx_ << " for row block starting at " << start_row;

    if (PREDICT_FALSE(start_row > block.last_updated_idx_)) {
      // The block to be updated completely falls after this delta block:
      //  <-- delta block -->      <-- delta block -->
      //                      <-- block to update     -->
      // This can happen because we don't know the block's last entry until after
      // we queued it in PrepareBatch(). We could potentially remove it at that
      // point during the prepare step, but for now just skip it here.
      continue;
    }

    rowid_t previous_rowidx = MathLimits<rowid_t>::kMax;
    bool continue_visit = true;
    for (int i = block.prepared_block_start_idx_; i < bpd.Count(); i++) {
      Slice slice = bpd.string_at_index(i);

      // Decode and check the ID of the row we're going to update.
      DeltaKey key;
      RETURN_NOT_OK(key.DecodeFrom(&slice));
      rowid_t row_idx = key.row_idx();

      // Check if the previous visitor notified us we don't need to apply more
      // mutations to this row and skip if we don't.
      if (row_idx == previous_rowidx && !continue_visit) {
        continue;
      } else {
        previous_rowidx = row_idx;
        continue_visit = true;
      }

      // Check that the delta is within the block we're currently processing.
      if (row_idx >= start_row + prepared_count_) {
        // Delta is for a row which comes after the block we're processing.
        return Status::OK();
      } else if (row_idx < start_row) {
        // Delta is for a row which comes before the block we're processing.
        continue;
      }
      RETURN_NOT_OK(visitor->Visit(key, slice, &continue_visit));
      if (VLOG_IS_ON(3)) {
        RowChangeList rcl(slice);
        DVLOG(3) << "Visited delta for key: " << key.ToString() << " Mut: "
            << rcl.ToString(*projection_) << " Continue?: "
            << (continue_visit ? "TRUE" : "FALSE");
      }
    }
  }

  return Status::OK();
}

// Returns whether a REDO mutation with 'timestamp' is relevant under 'snap'.
// If snap cannot include any mutations with a higher timestamp 'continue_visit' is
// set to false, it's set to true otherwise.
inline bool IsRedoRelevant(const MvccSnapshot& snap,
                            const Timestamp& timestamp,
                            bool* continue_visit) {
  *continue_visit = true;
  if (!snap.IsCommitted(timestamp)) {
    if (!snap.MayHaveCommittedTransactionsAtOrAfter(timestamp)) {
      *continue_visit = false;
    }
    return false;
  }
  return true;
}

// Returns whether an UNDO mutation with 'timestamp' is relevant under 'snap'.
// If snap cannot include any mutations with a lower timestamp 'continue_visit' is
// set to false, it's set to true otherwise.
inline bool IsUndoRelevant(const MvccSnapshot& snap,
                           const Timestamp& timestamp,
                           bool* continue_visit) {
  *continue_visit = true;
  if (snap.IsCommitted(timestamp)) {
    if (!snap.MayHaveUncommittedTransactionsAtOrBefore(timestamp)) {
      *continue_visit = false;
    }
    return false;
  }
  return true;
}

template<DeltaType Type>
struct ApplyingVisitor {

  Status Visit(const DeltaKey &key, const Slice &deltas, bool* continue_visit);

  inline Status ApplyMutation(const DeltaKey &key, const Slice &deltas) {
    int64_t rel_idx = key.row_idx() - dfi->prepared_idx_;
    DCHECK_GE(rel_idx, 0);

    // TODO: this code looks eerily similar to DMSIterator::ApplyUpdates!
    // I bet it can be combined.

    const Schema* schema = dfi->projection_;
    RowChangeListDecoder decoder((RowChangeList(deltas)));
    RETURN_NOT_OK(decoder.Init());
    if (decoder.is_update()) {
      return decoder.ApplyToOneColumn(rel_idx, dst, *schema, col_to_apply, dst->arena());
    } else if (decoder.is_delete()) {
      // If it's a DELETE, then it will be processed by DeletingVisitor.
      return Status::OK();
    } else {
      dfi->FatalUnexpectedDelta(key, deltas, "Expect only UPDATE or DELETE deltas on disk");
    }
    return Status::OK();
  }

  DeltaFileIterator *dfi;
  size_t col_to_apply;
  ColumnBlock *dst;
};

template<>
inline Status ApplyingVisitor<REDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas,
                                           bool* continue_visit) {
  if (IsRedoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    DVLOG(3) << "Applied redo delta";
    return ApplyMutation(key, deltas);
  }
  DVLOG(3) << "Redo delta uncommitted, skipped applying.";
  return Status::OK();
}

template<>
inline Status ApplyingVisitor<UNDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas,
                                           bool* continue_visit) {
  if (IsUndoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    DVLOG(3) << "Applied undo delta";
    return ApplyMutation(key, deltas);
  }
  DVLOG(3) << "Undo delta committed, skipped applying.";
  return Status::OK();
}

Status DeltaFileIterator::ApplyUpdates(size_t col_to_apply, ColumnBlock *dst) {
  DCHECK_LE(prepared_count_, dst->nrows());

  if (delta_type_ == REDO) {
    DVLOG(3) << "Applying REDO mutations to " << col_to_apply;
    ApplyingVisitor<REDO> visitor = {this, col_to_apply, dst};
    return VisitMutations(&visitor);
  } else {
    DVLOG(3) << "Applying UNDO mutations to " << col_to_apply;
    ApplyingVisitor<UNDO> visitor = {this, col_to_apply, dst};
    return VisitMutations(&visitor);
  }
}

// Visitor which applies deletes to the selection vector.
template<DeltaType Type>
struct DeletingVisitor {

  Status Visit(const DeltaKey &key, const Slice &deltas, bool* continue_visit);

  inline Status ApplyDelete(const DeltaKey &key, const Slice &deltas) {
    int64_t rel_idx = key.row_idx() - dfi->prepared_idx_;
    DCHECK_GE(rel_idx, 0);

    RowChangeListDecoder decoder((RowChangeList(deltas)));
    RETURN_NOT_OK(decoder.Init());
    if (decoder.is_update()) {
      DVLOG(3) << "Didn't delete row (update)";
      // If this is an update the row must be selected.
      DCHECK(sel_vec->IsRowSelected(rel_idx));
      return Status::OK();
    } else if (decoder.is_delete()) {
      DVLOG(3) << "Row deleted";
      sel_vec->SetRowUnselected(rel_idx);
    } else {
      dfi->FatalUnexpectedDelta(key, deltas, "Expect only UPDATE or DELETE deltas on disk");
    }
    return Status::OK();
  }

  DeltaFileIterator *dfi;
  SelectionVector *sel_vec;
};

template<>
inline Status DeletingVisitor<REDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas,
                                           bool* continue_visit) {
  if (IsRedoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    return ApplyDelete(key, deltas);
  }
  return Status::OK();
}

template<>
inline Status DeletingVisitor<UNDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas, bool*
                                           continue_visit) {
  if (IsUndoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    return ApplyDelete(key, deltas);
  }
  return Status::OK();
}


Status DeltaFileIterator::ApplyDeletes(SelectionVector *sel_vec) {
  DCHECK_LE(prepared_count_, sel_vec->nrows());
  if (delta_type_ == REDO) {
    DVLOG(3) << "Applying REDO deletes";
    DeletingVisitor<REDO> visitor = { this, sel_vec};
    return VisitMutations(&visitor);
  } else {
    DVLOG(3) << "Applying UNDO deletes";
    DeletingVisitor<UNDO> visitor = { this, sel_vec};
    return VisitMutations(&visitor);
  }
}

// Visitor which, for each mutation, appends it into a ColumnBlock of
// Mutation *s. See CollectMutations()
// Each mutation is projected into the iterator schema, if required.
template<DeltaType Type>
struct CollectingVisitor {

  Status Visit(const DeltaKey &key, const Slice &deltas, bool* continue_visit);

  Status Collect(const DeltaKey &key, const Slice &deltas) {
    int64_t rel_idx = key.row_idx() - dfi->prepared_idx_;
    DCHECK_GE(rel_idx, 0);

    RowChangeList changelist(deltas);
    Mutation *mutation = Mutation::CreateInArena(dst_arena, key.timestamp(), changelist);
    mutation->AppendToList(&dst->at(rel_idx));

    return Status::OK();
  }

  DeltaFileIterator *dfi;
  vector<Mutation *> *dst;
  Arena *dst_arena;
};

template<>
inline Status CollectingVisitor<REDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas,
                                           bool* continue_visit) {
  if (IsRedoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    return Collect(key, deltas);
  }
  return Status::OK();
}

template<>
inline Status CollectingVisitor<UNDO>::Visit(const DeltaKey& key,
                                           const Slice& deltas, bool*
                                           continue_visit) {
  if (IsUndoRelevant(dfi->mvcc_snap_, key.timestamp(), continue_visit)) {
    return Collect(key, deltas);
  }
  return Status::OK();
}

Status DeltaFileIterator::CollectMutations(vector<Mutation *> *dst, Arena *dst_arena) {
  DCHECK_LE(prepared_count_, dst->size());
  if (delta_type_ == REDO) {
    CollectingVisitor<REDO> visitor = {this, dst, dst_arena};
    return VisitMutations(&visitor);
  } else {
    CollectingVisitor<UNDO> visitor = {this, dst, dst_arena};
    return VisitMutations(&visitor);
  }
}

bool DeltaFileIterator::HasNext() {
  return !exhausted_ || !delta_blocks_.empty();
}

string DeltaFileIterator::ToString() const {
  return "DeltaFileIterator(" + dfr_->ToString() + ")";
}

struct FilterAndAppendVisitor {

  Status Visit(const DeltaKey& key, const Slice& deltas, bool* continue_visit) {

    // FilterAndAppendVisitor visitor visits all mutations.
    *continue_visit = true;

    faststring buf;
    RowChangeListEncoder enc(&buf);
    RETURN_NOT_OK(
        RowChangeListDecoder::RemoveColumnIdsFromChangeList(RowChangeList(deltas),
                                                            col_ids,
                                                            &enc));
    if (enc.is_initialized()) {
      RowChangeList rcl = enc.as_changelist();
      DeltaKeyAndUpdate upd;
      upd.key = key;
      CHECK(arena->RelocateSlice(rcl.slice(), &upd.cell));
      out->push_back(upd);
    }
    // if enc.is_initialized() return false, that means deltas only
    // contained the specified columns.
    return Status::OK();
  }

  const DeltaFileIterator* dfi;
  const vector<ColumnId>& col_ids;
  vector<DeltaKeyAndUpdate>* out;
  Arena* arena;
};

Status DeltaFileIterator::FilterColumnIdsAndCollectDeltas(
    const vector<ColumnId>& col_ids,
    vector<DeltaKeyAndUpdate>* out,
    Arena* arena) {
  FilterAndAppendVisitor visitor = {this, col_ids, out, arena};
  return VisitMutations(&visitor);
}

void DeltaFileIterator::FatalUnexpectedDelta(const DeltaKey &key, const Slice &deltas,
                                             const string &msg) {
  LOG(FATAL) << "Saw unexpected delta type in deltafile " << dfr_->ToString() << ": "
             << " rcl=" << RowChangeList(deltas).ToString(*projection_)
             << " key=" << key.ToString() << " (" << msg << ")";
}

} // namespace tablet
} // namespace kudu
