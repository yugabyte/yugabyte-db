//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/version_edit.h"

#include "db/version_set.h"
#include "util/coding.h"
#include "util/event_logger.h"
#include "util/sync_point.h"
#include "rocksdb/slice.h"

#include "rocksdb/db/version_edit.pb.h"
#include "rocksdb/metadata.h"

namespace rocksdb {

uint64_t PackFileNumberAndPathId(uint64_t number, uint64_t path_id) {
  assert(number <= kFileNumberMask);
  return number | (path_id * (kFileNumberMask + 1));
}

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

void FileMetaData::UpdateBoundaries(InternalKey key, const FileBoundaryValuesBase& source) {
  largest.key = std::move(key);
  if (smallest.key.empty()) {
    smallest.key = largest.key;
  }
  UpdateBoundariesExceptKey(source, UpdateBoundariesType::ALL);
}

void FileMetaData::UpdateBoundariesExceptKey(const FileBoundaryValuesBase& source,
                                             UpdateBoundariesType type) {
  if (type != UpdateBoundariesType::LARGEST) {
    smallest.seqno = std::min(smallest.seqno, source.seqno);
    for (const auto& user_value : source.user_values) {
      UpdateUserValue(&smallest.user_values, user_value, UpdateUserValueType::SMALLEST);
    }
  }
  if (type != UpdateBoundariesType::SMALLEST) {
    largest.seqno = std::max(largest.seqno, source.seqno);
    for (const auto& user_value : source.user_values) {
      UpdateUserValue(&largest.user_values, user_value, UpdateUserValueType::LARGEST);
    }
  }
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
  flushed_op_id_ = OpId();
}

void EncodeBoundaryValues(const FileBoundaryValues<InternalKey>& values, BoundaryValuesPB* out) {
  auto key = values.key.Encode();
  out->set_key(key.data(), key.size());
  out->set_seqno(values.seqno);

  for (const auto& user_value : values.user_values) {
    auto* value = out->add_user_values();
    value->set_tag(user_value->Tag());
    auto encoded_user_value = user_value->Encode();
    value->set_data(encoded_user_value.data(), encoded_user_value.size());
  }
}

Status DecodeBoundaryValues(BoundaryValuesExtractor* extractor,
                            const BoundaryValuesPB& values,
                            FileBoundaryValues<InternalKey>* out) {
  if (!values.has_key()) {
    return STATUS(Corruption, "key missing");
  }
  if (!values.has_seqno()) {
    return STATUS(Corruption, "seqno missing");
  }
  out->key = InternalKey::DecodeFrom(values.key());
  out->seqno = values.seqno();
  if (extractor != nullptr) {
    for (const auto &user_value : values.user_values()) {
      UserBoundaryValuePtr decoded;
      auto status = extractor->Decode(user_value.tag(), user_value.data(), &decoded);
      if (!status.ok()) {
        return status;
      }
      if (decoded) {
        out->user_values.push_back(std::move(decoded));
      }
    }
  }
  return Status();
}

bool VersionEdit::EncodeTo(std::string* dst) const {
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
  if (flushed_op_id_) {
    auto& op_id = *pb.mutable_last_op_id();
    op_id.set_term(flushed_op_id_.term);
    op_id.set_index(flushed_op_id_.index);
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
    auto& op_id = *new_file.mutable_last_op_id();
    op_id.set_term(f.last_op_id.term);
    op_id.set_index(f.last_op_id.index);
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
  if (pb.has_last_op_id()) {
    flushed_op_id_ = OpId(pb.last_op_id().term(), pb.last_op_id().index());
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
                             source.has_path_id() ? source.path_id() : 0,
                             source.total_file_size(),
                             source.base_file_size());
    auto status = DecodeBoundaryValues(extractor, source.smallest(), &meta.smallest);
    if (!status.ok()) {
      return status;
    }
    status = DecodeBoundaryValues(extractor, source.largest(), &meta.largest);
    if (!status.ok()) {
      return status;
    }
    meta.marked_for_compaction = source.has_marked_for_compaction() &&
                                 source.marked_for_compaction();
    max_level_ = std::max(max_level_, level);
    if (source.has_last_op_id()) {
      meta.last_op_id = OpId(source.last_op_id().term(), source.last_op_id().index());
    }
  }

  column_family_ = pb.has_column_family() ? pb.column_family() : 0;

  if (pb.has_column_family_name()) {
    column_family_name_ = pb.column_family_name();
  }

  is_column_family_drop_ = pb.has_is_column_family_drop() && pb.is_column_family_drop();

  return Status();
}

std::string VersionEdit::DebugString(bool hex_key) const {
  VersionEditPB pb;
  EncodeTo(&pb);
  return pb.DebugString();
}

std::string VersionEdit::DebugJSON(int edit_num, bool hex_key) const {
  JSONWriter jw;
  jw << "EditNumber" << edit_num;

  if (comparator_) {
    jw << "Comparator" << *comparator_;
  }
  if (log_number_) {
    jw << "LogNumber" << *log_number_;
  }
  if (prev_log_number_) {
    jw << "PrevLogNumber" << *prev_log_number_;
  }
  if (next_file_number_) {
    jw << "NextFileNumber" << *next_file_number_;
  }
  if (last_sequence_) {
    jw << "LastSeq" << *last_sequence_;
  }
  if (flushed_op_id_) {
    jw << "FlushedOpId";
    jw.StartObject();
    jw << "Term" << flushed_op_id_.term;
    jw << "Index" << flushed_op_id_.index;
    jw.EndObject();
  }

  if (!deleted_files_.empty()) {
    jw << "DeletedFiles";
    jw.StartArray();

    for (DeletedFileSet::const_iterator iter = deleted_files_.begin();
         iter != deleted_files_.end();
         ++iter) {
      jw.StartArrayedObject();
      jw << "Level" << iter->first;
      jw << "FileNumber" << iter->second;
      jw.EndArrayedObject();
    }

    jw.EndArray();
  }

  if (!new_files_.empty()) {
    jw << "AddedFiles";
    jw.StartArray();

    for (size_t i = 0; i < new_files_.size(); i++) {
      jw.StartArrayedObject();
      jw << "Level" << new_files_[i].first;
      const FileMetaData& f = new_files_[i].second;
      jw << "FileNumber" << f.fd.GetNumber();
      jw << "FileSize" << f.fd.GetTotalFileSize();
      jw << "SmallestIKey" << f.smallest.key.DebugString(hex_key);
      jw << "LargestIKey" << f.largest.key.DebugString(hex_key);
      jw.EndArrayedObject();
    }

    jw.EndArray();
  }

  jw << "ColumnFamily" << column_family_;

  if (column_family_name_) {
    jw << "ColumnFamilyAdd" << *column_family_name_;
  }
  if (is_column_family_drop_) {
    jw << "ColumnFamilyDrop" << std::string();
  }
  if (max_column_family_) {
    jw << "MaxColumnFamily" << *max_column_family_;
  }

  jw.EndObject();

  return jw.Get();
}

void VersionEdit::InitNewDB() {
  log_number_ = 0;
  next_file_number_ = VersionSet::kInitialNextFileNumber;
  last_sequence_ = 0;
  flushed_op_id_ = OpId();
}

}  // namespace rocksdb
