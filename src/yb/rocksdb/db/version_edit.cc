//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/db/version_edit.h"

#include "yb/rocksdb/db/version_edit.pb.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/metadata.h"
#include "yb/rocksdb/util/coding.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"

DEFINE_UNKNOWN_bool(use_per_file_metadata_for_flushed_frontier, false,
            "Allows taking per-file metadata in version edits into account when computing the "
            "flushed frontier.");

DEFINE_RUNTIME_bool(allow_sensitive_data_in_logs, false,
            "Allows potentially-sensitive debug data to be written unencrypted into logs.");

TAG_FLAG(use_per_file_metadata_for_flushed_frontier, hidden);
TAG_FLAG(use_per_file_metadata_for_flushed_frontier, advanced);

namespace rocksdb {

uint64_t PackFileNumberAndPathId(uint64_t number, uint64_t path_id) {
  assert(number <= kFileNumberMask);
  return number | (path_id * (kFileNumberMask + 1));
}

namespace {
  std::string SanitizeDebugStringHelper(const VersionEditPB& pb, bool short_debug) {
    if (!FLAGS_allow_sensitive_data_in_logs) {
      VersionEditPB pbcopy;
      pbcopy.CopyFrom(pb);
      pbcopy.clear_new_files();
      for(auto& file : pb.new_files()) {
        NewFilePB* new_file_copy = pbcopy.add_new_files();
        new_file_copy->CopyFrom(file);
        new_file_copy->clear_smallest();
        new_file_copy->clear_largest();
      }
      return short_debug ? pbcopy.ShortDebugString() : pbcopy.DebugString();
    }
    return short_debug ? pb.ShortDebugString() : pb.DebugString();
  }

  std::string SanitizeDebugString(const VersionEditPB& pb) {
    return SanitizeDebugStringHelper(pb, false /* short_debug */);
  }

  std::string SanitizeShortDebugString(const VersionEditPB& pb) {
    return SanitizeDebugStringHelper(pb, true /* short_debug */);
  }
}  // namespace

FileMetaData::FileMetaData()
    : refs(0),
    being_compacted(false),
    table_reader_handle(nullptr),
    compensated_file_size(0),
    num_entries(0),
    num_deletions(0),
    raw_key_size(0),
    raw_value_size(0),
    init_stats_from_file(false),
    marked_for_compaction(false) {
  smallest.seqno = kMaxSequenceNumber;
  largest.seqno = 0;
}

bool FileMetaData::Unref(TableCache* table_cache) {
  refs--;
  if (refs <= 0) {
    if (table_reader_handle) {
      DCHECK_ONLY_NOTNULL(table_cache);
      table_cache->ReleaseHandle(table_reader_handle);
      table_reader_handle = nullptr;
    }
    return true;
  } else {
    return false;
  }
}

void FileMetaData::UpdateKey(const Slice& key, UpdateBoundariesType type) {
  if (type != UpdateBoundariesType::kLargest) {
    smallest.key = InternalKey::DecodeFrom(key);
  }
  if (type != UpdateBoundariesType::kSmallest) {
    largest.key = InternalKey::DecodeFrom(key);
  }
}

void FileMetaData::UpdateBoundarySeqNo(SequenceNumber sequence_number) {
  smallest.seqno = std::min(smallest.seqno, sequence_number);
  largest.seqno = std::max(largest.seqno, sequence_number);
}

void FileMetaData::UpdateBoundaryUserValues(
    const UserBoundaryValueRefs& source, UpdateBoundariesType type) {
  if (type != UpdateBoundariesType::kLargest) {
    UpdateUserValues(source, UpdateUserValueType::kSmallest, &smallest.user_values);
  }
  if (type != UpdateBoundariesType::kSmallest) {
    UpdateUserValues(source, UpdateUserValueType::kLargest, &largest.user_values);
  }
}

void FileMetaData::UpdateBoundariesExceptKey(
    const BoundaryValues& source, UpdateBoundariesType type) {
  if (type != UpdateBoundariesType::kLargest) {
    smallest.seqno = std::min(smallest.seqno, source.seqno);
    UserFrontier::Update(
        source.user_frontier.get(), UpdateUserValueType::kSmallest, &smallest.user_frontier);
    UpdateUserValues(source.user_values, UpdateUserValueType::kSmallest, &smallest.user_values);
  }
  if (type != UpdateBoundariesType::kSmallest) {
    largest.seqno = std::max(largest.seqno, source.seqno);
    UserFrontier::Update(
        source.user_frontier.get(), UpdateUserValueType::kLargest, &largest.user_frontier);
    UpdateUserValues(source.user_values, UpdateUserValueType::kLargest, &largest.user_values);
  }
}

Slice FileMetaData::UserFilter() const {
  return largest.user_frontier ? largest.user_frontier->FilterAsSlice() : Slice();
}

std::string FileMetaData::FrontiersToString() const {
  return yb::Format("frontiers: { smallest: $0 largest: $1 }",
      smallest.user_frontier ? smallest.user_frontier->ToString() : "none",
      largest.user_frontier ? largest.user_frontier->ToString() : "none");
}

std::string FileMetaData::ToString() const {
  return yb::Format("{ number: $0 total_size: $1 base_size: $2 "
                    "being_compacted: $3 smallest: $4 largest: $5 }",
                    fd.GetNumber(), fd.GetTotalFileSize(), fd.GetBaseFileSize(),
                    being_compacted, smallest, largest);
}

void VersionEdit::Clear() {
  comparator_.reset();
  max_level_ = 0;
  log_number_.reset();
  prev_log_number_.reset();
  last_sequence_.reset();
  next_file_number_.reset();
  max_column_family_.reset();
  deleted_files_.clear();
  new_files_.clear();
  column_family_ = 0;
  column_family_name_.reset();
  is_column_family_drop_ = false;
  flushed_frontier_.reset();
}

void EncodeBoundaryValues(const FileBoundaryValues<InternalKey>& values, BoundaryValuesPB* out) {
  auto key = values.key.Encode();
  out->set_key(key.data(), key.size());
  out->set_seqno(values.seqno);
  if (values.user_frontier) {
    values.user_frontier->ToPB(out->mutable_user_frontier());
  }

  for (const auto& user_value : values.user_values) {
    auto* value = out->add_user_values();
    value->set_tag(user_value.tag);
    auto encoded_user_value = user_value.AsSlice();
    value->set_data(encoded_user_value.data(), encoded_user_value.size());
  }
}

Status DecodeBoundaryValues(BoundaryValuesExtractor* extractor,
                            const BoundaryValuesPB& values,
                            FileBoundaryValues<InternalKey>* out) {
  out->key = InternalKey::DecodeFrom(values.key());
  out->seqno = values.seqno();
  if (extractor != nullptr) {
    if (values.has_user_frontier()) {
      out->user_frontier = extractor->CreateFrontier();
      RETURN_NOT_OK(out->user_frontier->FromPB(values.user_frontier()));
    }
    out->user_values.reserve(values.user_values().size());
    for (const auto &user_value : values.user_values()) {
      if (user_value.data().empty()) {
        continue;
      }
      out->user_values.emplace_back(UserBoundaryValueRef {
        .tag = user_value.tag(),
        .value = user_value.data(),
      });
    }
  } else if (values.has_user_frontier()) {
    return STATUS_FORMAT(
        IllegalState, "Boundary values contains user frontier but extractor is not specified: $0",
        values);
  }
  return Status::OK();
}

bool VersionEdit::AppendEncodedTo(std::string* dst) const {
  VersionEditPB pb;
  auto result = EncodeTo(&pb);
  if (result) {
    pb.AppendToString(dst);
  }
  return result;
}

bool VersionEdit::EncodeTo(VersionEditPB* dst) const {
  VersionEditPB& pb = *dst;
  if (comparator_) {
    pb.set_comparator(*comparator_);
  }
  if (log_number_) {
    pb.set_log_number(*log_number_);
  }
  if (prev_log_number_) {
    pb.set_prev_log_number(*prev_log_number_);
  }
  if (next_file_number_) {
    pb.set_next_file_number(*next_file_number_);
  }
  if (last_sequence_) {
    pb.set_last_sequence(*last_sequence_);
  }
  if (flushed_frontier_) {
    flushed_frontier_->ToPB(pb.mutable_flushed_frontier());
  }
  if (max_column_family_) {
    pb.set_max_column_family(*max_column_family_);
  }

  for (const auto& deleted : deleted_files_) {
    auto& deleted_file = *pb.add_deleted_files();
    deleted_file.set_level(deleted.first);
    deleted_file.set_file_number(deleted.second);
  }

  for (size_t i = 0; i < new_files_.size(); i++) {
    const FileMetaData& f = new_files_[i].second;
    if (!f.smallest.key.Valid() || !f.largest.key.Valid()) {
      return false;
    }
    auto& new_file = *pb.add_new_files();
    new_file.set_level(new_files_[i].first);
    new_file.set_number(f.fd.GetNumber());
    new_file.set_total_file_size(f.fd.GetTotalFileSize());
    new_file.set_base_file_size(f.fd.GetBaseFileSize());
    EncodeBoundaryValues(f.smallest, new_file.mutable_smallest());
    EncodeBoundaryValues(f.largest, new_file.mutable_largest());
    if (f.fd.GetPathId() != 0) {
      new_file.set_path_id(f.fd.GetPathId());
    }
    if (f.marked_for_compaction) {
      new_file.set_marked_for_compaction(true);
    }
    if (f.imported) {
      new_file.set_imported(true);
    }
  }

  // 0 is default and does not need to be explicitly written
  if (column_family_ != 0) {
    pb.set_column_family(column_family_);
  }

  if (column_family_name_) {
    pb.set_column_family_name(*column_family_name_);
  }

  if (is_column_family_drop_) {
    pb.set_is_column_family_drop(true);
  }

  return true;
}

Status VersionEdit::DecodeFrom(BoundaryValuesExtractor* extractor, const Slice& src) {
  Clear();
  VersionEditPB pb;
  if (!pb.ParseFromArray(src.data(), static_cast<int>(src.size()))) {
    return STATUS(Corruption, "VersionEdit");
  }

  VLOG(1) << "Parsed version edit: " << SanitizeShortDebugString(pb);

  if (pb.has_comparator()) {
    comparator_ = std::move(*pb.mutable_comparator());
  }
  if (pb.has_log_number()) {
    log_number_ = pb.log_number();
  }
  if (pb.has_prev_log_number()) {
    prev_log_number_ = pb.prev_log_number();
  }
  if (pb.has_next_file_number()) {
    next_file_number_ = pb.next_file_number();
  }
  if (pb.has_last_sequence()) {
    last_sequence_ = pb.last_sequence();
  }
  if (extractor) {
    // BoundaryValuesExtractor could be not set when running from internal RocksDB tools.
    if (pb.has_obsolete_last_op_id()) {
      flushed_frontier_ = extractor->CreateFrontier();
      flushed_frontier_->FromOpIdPBDeprecated(pb.obsolete_last_op_id());
    }
    if (pb.has_flushed_frontier()) {
      flushed_frontier_ = extractor->CreateFrontier();
      RETURN_NOT_OK(flushed_frontier_->FromPB(pb.flushed_frontier()));
    }
  }
  if (pb.has_max_column_family()) {
    max_column_family_ = pb.max_column_family();
  }

  for (const auto& deleted : pb.deleted_files()) {
    deleted_files_.emplace(deleted.level(), deleted.file_number());
  }

  const size_t new_files_size = static_cast<size_t>(pb.new_files_size());
  new_files_.resize(new_files_size);

  for (size_t i = 0; i < new_files_size; ++i) {
    auto& source = pb.new_files(static_cast<int>(i));
    int level = source.level();
    new_files_[i].first = level;
    auto& meta = new_files_[i].second;
    meta.fd = FileDescriptor(source.number(),
                             source.path_id(),
                             source.total_file_size(),
                             source.base_file_size());
    if (source.has_obsolete_last_op_id() && extractor) {
      meta.largest.user_frontier = extractor->CreateFrontier();
      meta.largest.user_frontier->FromOpIdPBDeprecated(source.obsolete_last_op_id());
    }
    auto status = DecodeBoundaryValues(extractor, source.smallest(), &meta.smallest);
    if (!status.ok()) {
      return status;
    }
    status = DecodeBoundaryValues(extractor, source.largest(), &meta.largest);
    if (!status.ok()) {
      return status;
    }
    meta.marked_for_compaction = source.marked_for_compaction();
    max_level_ = std::max(max_level_, level);
    meta.imported = source.imported();

    // Use the relevant fields in the "largest" frontier to update the "flushed" frontier for this
    // version edit. In practice this will only look at OpId and will discard hybrid time and
    // history cutoff (which probably won't be there anyway) coming from the boundary values.
    //
    // This is enabled only if --use_per_file_metadata_for_flushed_frontier is specified, until we
    // know that we don't have any clusters with wrong per-file flushed frontier metadata in version
    // edits, such as that restored from old backups from unrelated clusters.
    if (FLAGS_use_per_file_metadata_for_flushed_frontier && meta.largest.user_frontier) {
      if (!flushed_frontier_) {
        LOG(DFATAL) << "Flushed frontier not present but a file's largest user frontier present: "
                    << meta.largest.user_frontier->ToString()
                    << ", version edit protobuf:\n" << SanitizeDebugString(pb);
      } else if (!flushed_frontier_->Dominates(*meta.largest.user_frontier,
                                               UpdateUserValueType::kLargest)) {
        // The flushed frontier of this VersionEdit must already include the information provided
        // by flushed frontiers of individual files.
        LOG(DFATAL) << "Flushed frontier is present but has to be updated with data from "
                    << "file boundary: flushed_frontier=" << flushed_frontier_->ToString()
                    << ", a file's larget user frontier: "
                    << meta.largest.user_frontier->ToString()
                    << ", version edit protobuf:\n" << SanitizeDebugString(pb);
      }
      UpdateUserFrontier(
          &flushed_frontier_, meta.largest.user_frontier, UpdateUserValueType::kLargest);
    }
  }

  column_family_ = pb.column_family();

  if (!pb.column_family_name().empty()) {
    column_family_name_ = pb.column_family_name();
  }

  is_column_family_drop_ = pb.is_column_family_drop();

  return Status();
}

std::string VersionEdit::DebugString(bool hex_key) const {
  VersionEditPB pb;
  EncodeTo(&pb);
  return SanitizeDebugString(pb);
}

void VersionEdit::InitNewDB() {
  log_number_ = 0;
  next_file_number_ = VersionSet::kInitialNextFileNumber;
  last_sequence_ = 0;
  flushed_frontier_.reset();
}

void VersionEdit::AddTestFile(int level,
                              const FileDescriptor& fd,
                              const FileMetaData::BoundaryValues& smallest,
                              const FileMetaData::BoundaryValues& largest,
                              bool marked_for_compaction) {
  DCHECK_LE(smallest.seqno, largest.seqno);
  FileMetaData f;
  f.fd = fd;
  f.fd.table_reader = nullptr;
  f.smallest = smallest;
  f.largest = largest;
  f.marked_for_compaction = marked_for_compaction;
  new_files_.emplace_back(level, f);
}

void VersionEdit::AddFile(int level, const FileMetaData& f) {
  DCHECK_LE(f.smallest.seqno, f.largest.seqno);
  new_files_.emplace_back(level, f);
}

void VersionEdit::AddCleanedFile(int level, const FileMetaData& f) {
  DCHECK_LE(f.smallest.seqno, f.largest.seqno);
  FileMetaData nf;
  nf.fd = f.fd;
  nf.fd.table_reader = nullptr;
  nf.smallest = f.smallest;
  nf.largest = f.largest;
  nf.marked_for_compaction = f.marked_for_compaction;
  nf.imported = f.imported;
  new_files_.emplace_back(level, std::move(nf));
}

void VersionEdit::UpdateFlushedFrontier(UserFrontierPtr value) {
  ModifyFlushedFrontier(std::move(value), FrontierModificationMode::kUpdate);
}

void VersionEdit::ModifyFlushedFrontier(UserFrontierPtr value, FrontierModificationMode mode) {
  if (mode == FrontierModificationMode::kForce) {
    flushed_frontier_ = std::move(value);
    force_flushed_frontier_ = true;
  } else {
    UpdateUserFrontier(&flushed_frontier_, std::move(value), UpdateUserValueType::kLargest);
  }
}

std::string FileDescriptor::ToString() const {
  return yb::Format("{ number: $0 path_id: $1 total_file_size: $2 base_file_size: $3 }",
                    GetNumber(), GetPathId(), total_file_size, base_file_size);
}

}  // namespace rocksdb
