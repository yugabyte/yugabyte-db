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

#include <algorithm>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "kudu/cfile/bloomfile.h"
#include "kudu/cfile/cfile_util.h"
#include "kudu/cfile/cfile_writer.h"
#include "kudu/common/scan_spec.h"
#include "kudu/gutil/algorithm.h"
#include "kudu/gutil/dynamic_annotations.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/diskrowset.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/util/flag_tags.h"

DEFINE_bool(consult_bloom_filters, true, "Whether to consult bloom filters on row presence checks");
TAG_FLAG(consult_bloom_filters, hidden);

namespace kudu {
namespace tablet {

using cfile::ReaderOptions;
using cfile::DefaultColumnValueIterator;
using fs::ReadableBlock;
using std::shared_ptr;
using strings::Substitute;

////////////////////////////////////////////////////////////
// Utilities
////////////////////////////////////////////////////////////

static Status OpenReader(const shared_ptr<RowSetMetadata>& rowset_metadata,
                         ColumnId col_id,
                         gscoped_ptr<CFileReader> *new_reader) {
  FsManager* fs = rowset_metadata->fs_manager();
  gscoped_ptr<ReadableBlock> block;
  BlockId block_id = rowset_metadata->column_data_block_for_col_id(col_id);
  RETURN_NOT_OK(fs->OpenBlock(block_id, &block));

  // TODO: somehow pass reader options in schema
  ReaderOptions opts;
  return CFileReader::OpenNoInit(block.Pass(), opts, new_reader);
}

////////////////////////////////////////////////////////////
// CFile Base
////////////////////////////////////////////////////////////

CFileSet::CFileSet(shared_ptr<RowSetMetadata> rowset_metadata)
    : rowset_metadata_(std::move(rowset_metadata)) {}

CFileSet::~CFileSet() {
}


Status CFileSet::Open() {
  RETURN_NOT_OK(OpenBloomReader());

  // Lazily open the column data cfiles. Each one will be fully opened
  // later, when the first iterator seeks for the first time.
  RowSetMetadata::ColumnIdToBlockIdMap block_map = rowset_metadata_->GetColumnBlocksById();
  for (const RowSetMetadata::ColumnIdToBlockIdMap::value_type& e : block_map) {
    ColumnId col_id = e.first;
    DCHECK(!ContainsKey(readers_by_col_id_, col_id)) << "already open";

    gscoped_ptr<CFileReader> reader;
    RETURN_NOT_OK(OpenReader(rowset_metadata_, col_id, &reader));
    readers_by_col_id_[col_id] = shared_ptr<CFileReader>(reader.release());
    VLOG(1) << "Successfully opened cfile for column id " << col_id
            << " in " << rowset_metadata_->ToString();
  }

  // However, the key reader should always be fully opened, so that we
  // can figure out where in the rowset tree we belong.
  if (rowset_metadata_->has_adhoc_index_block()) {
    RETURN_NOT_OK(OpenAdHocIndexReader());
  } else {
    RETURN_NOT_OK(key_index_reader()->Init());
  }

  // Determine the upper and lower key bounds for this CFileSet.
  RETURN_NOT_OK(LoadMinMaxKeys());

  return Status::OK();
}

Status CFileSet::OpenAdHocIndexReader() {
  if (ad_hoc_idx_reader_ != nullptr) {
    return Status::OK();
  }

  FsManager* fs = rowset_metadata_->fs_manager();
  gscoped_ptr<ReadableBlock> block;
  RETURN_NOT_OK(fs->OpenBlock(rowset_metadata_->adhoc_index_block(), &block));

  ReaderOptions opts;
  return CFileReader::Open(block.Pass(), opts, &ad_hoc_idx_reader_);
}


Status CFileSet::OpenBloomReader() {
  if (bloom_reader_ != nullptr) {
    return Status::OK();
  }

  FsManager* fs = rowset_metadata_->fs_manager();
  gscoped_ptr<ReadableBlock> block;
  RETURN_NOT_OK(fs->OpenBlock(rowset_metadata_->bloom_block(), &block));

  ReaderOptions opts;
  Status s = BloomFileReader::OpenNoInit(block.Pass(), opts, &bloom_reader_);
  if (!s.ok()) {
    LOG(WARNING) << "Unable to open bloom file in " << rowset_metadata_->ToString() << ": "
                 << s.ToString();
    // Continue without bloom.
  }

  return Status::OK();
}

Status CFileSet::LoadMinMaxKeys() {
  CFileReader *key_reader = key_index_reader();
  if (!key_reader->GetMetadataEntry(DiskRowSet::kMinKeyMetaEntryName, &min_encoded_key_)) {
    return Status::Corruption("No min key found", ToString());
  }
  if (!key_reader->GetMetadataEntry(DiskRowSet::kMaxKeyMetaEntryName, &max_encoded_key_)) {
    return Status::Corruption("No max key found", ToString());
  }
  if (Slice(min_encoded_key_).compare(max_encoded_key_) > 0) {
    return Status::Corruption(StringPrintf("Min key %s > max key %s",
                                           Slice(min_encoded_key_).ToDebugString().c_str(),
                                           Slice(max_encoded_key_).ToDebugString().c_str()),
                              ToString());
  }

  return Status::OK();
}

CFileReader* CFileSet::key_index_reader() const {
  if (ad_hoc_idx_reader_) {
    return ad_hoc_idx_reader_.get();
  }
  // If there is no special index cfile, then we have a non-compound key
  // and we can just use the key column.
  // This is always the first column listed in the tablet schema.
  int key_col_id = tablet_schema().column_id(0);
  return FindOrDie(readers_by_col_id_, key_col_id).get();
}

Status CFileSet::NewColumnIterator(ColumnId col_id, CFileReader::CacheControl cache_blocks,
                                   CFileIterator **iter) const {
  return FindOrDie(readers_by_col_id_, col_id)->NewIterator(iter, cache_blocks);
}

CFileSet::Iterator *CFileSet::NewIterator(const Schema *projection) const {
  return new CFileSet::Iterator(shared_from_this(), projection);
}

Status CFileSet::CountRows(rowid_t *count) const {
  return key_index_reader()->CountRows(count);
}

Status CFileSet::GetBounds(Slice *min_encoded_key,
                           Slice *max_encoded_key) const {
  *min_encoded_key = Slice(min_encoded_key_);
  *max_encoded_key = Slice(max_encoded_key_);
  return Status::OK();
}

uint64_t CFileSet::EstimateOnDiskSize() const {
  uint64_t ret = 0;
  for (const ReaderMap::value_type& e : readers_by_col_id_) {
    const shared_ptr<CFileReader> &reader = e.second;
    ret += reader->file_size();
  }
  return ret;
}

Status CFileSet::FindRow(const RowSetKeyProbe &probe, rowid_t *idx,
                         ProbeStats* stats) const {
  if (bloom_reader_ != nullptr && FLAGS_consult_bloom_filters) {
    // Fully open the BloomFileReader if it was lazily opened earlier.
    //
    // If it's already initialized, this is a no-op.
    RETURN_NOT_OK(bloom_reader_->Init());

    stats->blooms_consulted++;
    bool present;
    Status s = bloom_reader_->CheckKeyPresent(probe.bloom_probe(), &present);
    if (s.ok() && !present) {
      return Status::NotFound("not present in bloom filter");
    } else if (!s.ok()) {
      LOG(WARNING) << "Unable to query bloom: " << s.ToString()
                   << " (disabling bloom for this rowset from this point forward)";
      const_cast<CFileSet *>(this)->bloom_reader_.reset(nullptr);
      // Continue with the slow path
    }
  }

  stats->keys_consulted++;
  CFileIterator *key_iter = nullptr;
  RETURN_NOT_OK(NewKeyIterator(&key_iter));

  gscoped_ptr<CFileIterator> key_iter_scoped(key_iter); // free on return

  bool exact;
  RETURN_NOT_OK(key_iter->SeekAtOrAfter(probe.encoded_key(), &exact));
  if (!exact) {
    return Status::NotFound("not present in storefile (failed seek)");
  }

  *idx = key_iter->GetCurrentOrdinal();
  return Status::OK();
}

Status CFileSet::CheckRowPresent(const RowSetKeyProbe &probe, bool *present,
                                 rowid_t *rowid, ProbeStats* stats) const {

  Status s = FindRow(probe, rowid, stats);
  if (s.IsNotFound()) {
    // In the case that the key comes past the end of the file, Seek
    // will return NotFound. In that case, it is OK from this function's
    // point of view - just a non-present key.
    *present = false;
    return Status::OK();
  }
  *present = true;
  return s;
}

Status CFileSet::NewKeyIterator(CFileIterator **key_iter) const {
  return key_index_reader()->NewIterator(key_iter, CFileReader::CACHE_BLOCK);
}

////////////////////////////////////////////////////////////
// Iterator
////////////////////////////////////////////////////////////
CFileSet::Iterator::~Iterator() {
  STLDeleteElements(&col_iters_);
}

Status CFileSet::Iterator::CreateColumnIterators(const ScanSpec* spec) {
  DCHECK_EQ(0, col_iters_.size());
  vector<ColumnIterator*> ret_iters;
  ElementDeleter del(&ret_iters);
  ret_iters.reserve(projection_->num_columns());

  CFileReader::CacheControl cache_blocks = CFileReader::CACHE_BLOCK;
  if (spec && !spec->cache_blocks()) {
    cache_blocks = CFileReader::DONT_CACHE_BLOCK;
  }

  for (int proj_col_idx = 0;
       proj_col_idx < projection_->num_columns();
       proj_col_idx++) {
    ColumnId col_id = projection_->column_id(proj_col_idx);

    if (!base_data_->has_data_for_column_id(col_id)) {
      // If we have no data for a column, most likely it was added via an ALTER
      // operation after this CFileSet was flushed. In that case, we're guaranteed
      // that it is either NULLable, or has a "read-default". Otherwise, consider it a corruption.
      const ColumnSchema& col_schema = projection_->column(proj_col_idx);
      if (PREDICT_FALSE(!col_schema.is_nullable() && !col_schema.has_read_default())) {
        return Status::Corruption(Substitute("column $0 has no data in rowset $1",
                                             col_schema.ToString(), base_data_->ToString()));
      }
      ret_iters.push_back(new DefaultColumnValueIterator(col_schema.type_info(),
                                                         col_schema.read_default_value()));
      continue;
    }
    CFileIterator *iter;
    RETURN_NOT_OK_PREPEND(base_data_->NewColumnIterator(col_id, cache_blocks, &iter),
                          Substitute("could not create iterator for column $0",
                                     projection_->column(proj_col_idx).ToString()));
    ret_iters.push_back(iter);
  }

  col_iters_.swap(ret_iters);
  return Status::OK();
}

Status CFileSet::Iterator::Init(ScanSpec *spec) {
  CHECK(!initted_);

  // Setup Key Iterator
  CFileIterator *tmp;
  RETURN_NOT_OK(base_data_->NewKeyIterator(&tmp));
  key_iter_.reset(tmp);

  // Setup column iterators.
  RETURN_NOT_OK(CreateColumnIterators(spec));

  // If there is a range predicate on the key column, push that down into an
  // ordinal range.
  RETURN_NOT_OK(PushdownRangeScanPredicate(spec));

  initted_ = true;

  // Don't actually seek -- we'll seek when we first actually read the
  // data.
  cur_idx_ = lower_bound_idx_;
  Unprepare(); // Reset state.
  return Status::OK();
}

Status CFileSet::Iterator::PushdownRangeScanPredicate(ScanSpec *spec) {
  CHECK_GT(row_count_, 0);

  lower_bound_idx_ = 0;
  upper_bound_idx_ = row_count_;

  if (spec == nullptr) {
    // No predicate.
    return Status::OK();
  }

  Schema key_schema_for_vlog;
  if (VLOG_IS_ON(1)) {
    key_schema_for_vlog = base_data_->tablet_schema().CreateKeyProjection();
  }

  if (spec->lower_bound_key() &&
      spec->lower_bound_key()->encoded_key().compare(base_data_->min_encoded_key_) > 0) {
    bool exact;
    Status s = key_iter_->SeekAtOrAfter(*spec->lower_bound_key(), &exact);
    if (s.IsNotFound()) {
      // The lower bound is after the end of the key range.
      // Thus, no rows will pass the predicate, so we set the lower bound
      // to the end of the file.
      lower_bound_idx_ = row_count_;
      return Status::OK();
    }
    RETURN_NOT_OK(s);

    lower_bound_idx_ = std::max(lower_bound_idx_, key_iter_->GetCurrentOrdinal());
    VLOG(1) << "Pushed lower bound value "
            << spec->lower_bound_key()->Stringify(key_schema_for_vlog)
            << " as row_idx >= " << lower_bound_idx_;
  }
  if (spec->exclusive_upper_bound_key() &&
      spec->exclusive_upper_bound_key()->encoded_key().compare(
          base_data_->max_encoded_key_) <= 0) {
    bool exact;
    Status s = key_iter_->SeekAtOrAfter(*spec->exclusive_upper_bound_key(), &exact);
    if (PREDICT_FALSE(s.IsNotFound())) {
      LOG(DFATAL) << "CFileSet indicated upper bound was within range, but "
                  << "key iterator could not seek. "
                  << "CFileSet upper_bound = "
                  << Slice(base_data_->max_encoded_key_).ToDebugString()
                  << ", enc_key = "
                  << spec->exclusive_upper_bound_key()->encoded_key().ToDebugString();
    } else {
      RETURN_NOT_OK(s);

      rowid_t cur = key_iter_->GetCurrentOrdinal();
      upper_bound_idx_ = std::min(upper_bound_idx_, cur);

      VLOG(1) << "Pushed upper bound value "
              << spec->exclusive_upper_bound_key()->Stringify(key_schema_for_vlog)
              << " as row_idx < " << upper_bound_idx_;
    }
  }
  return Status::OK();
}

void CFileSet::Iterator::Unprepare() {
  prepared_count_ = 0;
  cols_prepared_.assign(col_iters_.size(), false);
}

Status CFileSet::Iterator::PrepareBatch(size_t *n) {
  DCHECK_EQ(prepared_count_, 0) << "Already prepared";

  size_t remaining = upper_bound_idx_ - cur_idx_;
  if (*n > remaining) {
    *n = remaining;
  }

  prepared_count_ = *n;

  // Lazily prepare the first column when it is materialized.
  return Status::OK();
}


Status CFileSet::Iterator::PrepareColumn(size_t idx) {
  if (cols_prepared_[idx]) {
    // Already prepared in this batch.
    return Status::OK();
  }

  ColumnIterator* col_iter = col_iters_[idx];
  size_t n = prepared_count_;

  if (!col_iter->seeked() || col_iter->GetCurrentOrdinal() != cur_idx_) {
    // Either this column has not yet been accessed, or it was accessed
    // but then skipped in a prior block (e.g because predicates on other
    // columns completely eliminated the block).
    //
    // Either way, we need to seek it to the correct offset.
    RETURN_NOT_OK(col_iter->SeekToOrdinal(cur_idx_));
  }

  Status s = col_iter->PrepareBatch(&n);
  if (!s.ok()) {
    LOG(WARNING) << "Unable to prepare column " << idx << ": " << s.ToString();
    return s;
  }

  if (n != prepared_count_) {
    return Status::Corruption(
      StringPrintf("Column %zd (%s) didn't yield enough rows at offset %zd: expected "
                   "%zd but only got %zd", idx, projection_->column(idx).ToString().c_str(),
                   cur_idx_, prepared_count_, n));
  }

  cols_prepared_[idx] = true;

  return Status::OK();
}

Status CFileSet::Iterator::InitializeSelectionVector(SelectionVector *sel_vec) {
  sel_vec->SetAllTrue();
  return Status::OK();
}

Status CFileSet::Iterator::MaterializeColumn(size_t col_idx, ColumnBlock *dst) {
  CHECK_EQ(prepared_count_, dst->nrows());
  DCHECK_LT(col_idx, col_iters_.size());

  RETURN_NOT_OK(PrepareColumn(col_idx));
  ColumnIterator* iter = col_iters_[col_idx];
  return iter->Scan(dst);
}

Status CFileSet::Iterator::FinishBatch() {
  CHECK_GT(prepared_count_, 0);

  for (size_t i = 0; i < col_iters_.size(); i++) {
    if (cols_prepared_[i]) {
      Status s = col_iters_[i]->FinishBatch();
      if (!s.ok()) {
        LOG(WARNING) << "Unable to FinishBatch() on column " << i;
        return s;
      }
    }
  }

  cur_idx_ += prepared_count_;
  Unprepare();

  return Status::OK();
}


void CFileSet::Iterator::GetIteratorStats(vector<IteratorStats>* stats) const {
  stats->clear();
  stats->reserve(col_iters_.size());
  for (const ColumnIterator* iter : col_iters_) {
    ANNOTATE_IGNORE_READS_BEGIN();
    stats->push_back(iter->io_statistics());
    ANNOTATE_IGNORE_READS_END();
  }
}

} // namespace tablet
} // namespace kudu
