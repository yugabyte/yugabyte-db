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

#include "yb/rocksdb/db/compaction.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include <algorithm>
#include <vector>

#include "yb/gutil/stl_util.h"

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/compaction_picker.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/util/logging.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/sync_point.h"

using namespace yb::size_literals;

namespace rocksdb {

namespace {

Slice SliceDup(Arena* arena, Slice input) {
  auto* mem = arena->AllocateAligned(input.size());
  memcpy(mem, input.data(), input.size());
  return Slice(mem, input.size());
}

}

LightweightBoundaries::LightweightBoundaries(Arena* arena,
                                             const FileMetaData::BoundaryValues& source) {
  key = SliceDup(arena, source.key.Encode());
  num_user_values = source.user_values.size();
  user_tags = reinterpret_cast<UserBoundaryTag*>(
    arena->AllocateAligned(sizeof(UserBoundaryTag) * num_user_values));
  user_values = reinterpret_cast<Slice*>(
    arena->AllocateAligned(sizeof(Slice) * num_user_values));
  for (size_t i = 0; i != num_user_values; ++i) {
    const UserBoundaryValue& value = source.user_values[i];
    new (user_tags + i) UserBoundaryTag(value.tag);
    new (user_values + i) Slice(SliceDup(arena, value.AsSlice()));
  }
}

FdWithBoundaries::FdWithBoundaries(Arena* arena, const FileMetaData& source)
    : fd(source.fd), smallest(arena, source.smallest), largest(arena, source.largest) {
  if (source.largest.user_frontier) {
    auto filter = source.largest.user_frontier->FilterAsSlice();
    if (!filter.empty()) {
      user_filter_data = SliceDup(arena, filter);
    }
  }
}

uint64_t TotalFileSize(const std::vector<FileMetaData*>& files) {
  uint64_t sum = 0;
  for (size_t i = 0; i < files.size() && files[i]; i++) {
    sum += files[i]->fd.GetTotalFileSize();
  }
  return sum;
}

bool Compaction::IsCompactionStyleUniversal() const {
  return cfd_->ioptions()->compaction_style == kCompactionStyleUniversal;
}

void Compaction::SetInputVersion(Version* input_version) {
  input_version_number_ = input_version->GetVersionNumber();
  input_version_level0_non_overlapping_ = input_version->storage_info()->level0_non_overlapping();
  vset_ = input_version->version_set();
  cfd_ = input_version->cfd();
  cfd_->Ref();

  if (IsCompactionStyleUniversal()) {
    // We don't need to lock the whole input version for universal compaction, only need input
    // files.
    for (auto& input_level : inputs_) {
      for (auto* f : input_level.files) {
        ++f->refs;
      }
    }
  } else {
    input_version_ = input_version;
    input_version_->Ref();
  }

  edit_.SetColumnFamily(cfd_->GetID());
}

void Compaction::GetBoundaryKeys(
    VersionStorageInfo* vstorage,
    const std::vector<CompactionInputFiles>& inputs, Slice* smallest_user_key,
    Slice* largest_user_key) {
  bool initialized = false;
  const Comparator* ucmp = vstorage->InternalComparator()->user_comparator();
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (inputs[i].files.empty()) {
      continue;
    }
    if (inputs[i].level == 0) {
      // we need to consider all files on level 0
      for (const auto* f : inputs[i].files) {
        Slice start_user_key = f->smallest.key.user_key();
        if (!initialized ||
            ucmp->Compare(start_user_key, *smallest_user_key) < 0) {
          *smallest_user_key = start_user_key;
        }
        Slice end_user_key = f->largest.key.user_key();
        if (!initialized ||
            ucmp->Compare(end_user_key, *largest_user_key) > 0) {
          *largest_user_key = end_user_key;
        }
        initialized = true;
      }
    } else {
      // we only need to consider the first and last file
      Slice start_user_key = inputs[i].files[0]->smallest.key.user_key();
      if (!initialized ||
          ucmp->Compare(start_user_key, *smallest_user_key) < 0) {
        *smallest_user_key = start_user_key;
      }
      Slice end_user_key = inputs[i].files.back()->largest.key.user_key();
      if (!initialized || ucmp->Compare(end_user_key, *largest_user_key) > 0) {
        *largest_user_key = end_user_key;
      }
      initialized = true;
    }
  }
}

// helper function to determine if compaction is creating files at the
// bottommost level
bool Compaction::IsBottommostLevel(
    int output_level, VersionStorageInfo* vstorage,
    const std::vector<CompactionInputFiles>& inputs) {
  if (inputs[0].level == 0 &&
      inputs[0].files.back() != vstorage->LevelFiles(0).back()) {
    return false;
  }

  Slice smallest_key, largest_key;
  GetBoundaryKeys(vstorage, inputs, &smallest_key, &largest_key);

  // Checks whether there are files living beyond the output_level.
  // If lower levels have files, it checks for overlap between files
  // if the compaction process and those files.
  // Bottomlevel optimizations can be made if there are no files in
  // lower levels or if there is no overlap with the files in
  // the lower levels.
  for (int i = output_level + 1; i < vstorage->num_levels(); i++) {
    // It is not the bottommost level if there are files in higher
    // levels when the output level is 0 or if there are files in
    // higher levels which overlap with files to be compacted.
    // output_level == 0 means that we want it to be considered
    // s the bottommost level only if the last file on the level
    // is a part of the files to be compacted - this is verified by
    // the first if condition in this function
    if (vstorage->NumLevelFiles(i) > 0 &&
        (output_level == 0 ||
         vstorage->OverlapInLevel(i, &smallest_key, &largest_key))) {
      return false;
    }
  }
  return true;
}

// test function to validate the functionality of IsBottommostLevel()
// function -- determines if compaction with inputs and storage is bottommost
bool Compaction::TEST_IsBottommostLevel(
    int output_level, VersionStorageInfo* vstorage,
    const std::vector<CompactionInputFiles>& inputs) {
  return IsBottommostLevel(output_level, vstorage, inputs);
}

bool Compaction::IsFullCompaction(
    VersionStorageInfo* vstorage,
    const std::vector<CompactionInputFiles>& inputs) {
  size_t num_files_in_compaction = 0;
  size_t total_num_files = 0;
  for (int l = 0; l < vstorage->num_levels(); l++) {
    total_num_files += vstorage->NumLevelFiles(l);
  }
  for (size_t i = 0; i < inputs.size(); i++) {
    num_files_in_compaction += inputs[i].size();
  }
  return num_files_in_compaction == total_num_files;
}

std::unique_ptr<Compaction> Compaction::Create(
    VersionStorageInfo* vstorage, const MutableCFOptions& _mutable_cf_options,
    std::vector<CompactionInputFiles> inputs, int output_level, uint64_t target_file_size,
    uint64_t max_grandparent_overlap_bytes, uint32_t output_path_id, CompressionType compression,
    std::vector<FileMetaData*> grandparents, Logger* info_log, bool manual_compaction, double score,
    bool deletion_compaction, CompactionReason compaction_reason) {
  bool has_input_files = false;
  for (auto& input : inputs) {
    yb::EraseIf([info_log](FileMetaData* file) {
      bool being_deleted = file->being_deleted;
      if (being_deleted) {
        RLOG(
            InfoLogLevel::INFO_LEVEL, info_log,
            yb::Format("Skipping compaction of file that is being deleted: $0", file).c_str());
      }
      return being_deleted;
    }, &input.files);
    has_input_files |= !input.empty();
  }
  if (!has_input_files) {
    RLOG(
        InfoLogLevel::INFO_LEVEL, info_log,
        "Skipping compaction creation, no input files to compact");
    return nullptr;
  }
  // We don't remove empty input levels, because empty input levels are handled differently
  // than absent ones, for example by Compaction::IsTrivialMove.
  // But we need to remove inputs[0] if it is empty and has level 0, otherwise
  // Compaction::IsBottommostLevel will fail.
  if (inputs[0].level == 0 && inputs[0].empty()) {
    inputs.erase(inputs.begin());
  }

  return std::unique_ptr<Compaction>(new Compaction(
      vstorage, _mutable_cf_options, inputs, output_level, target_file_size,
      max_grandparent_overlap_bytes, output_path_id, compression, grandparents, manual_compaction,
      score, deletion_compaction, compaction_reason));
}

Compaction::Compaction(VersionStorageInfo* vstorage,
                       const MutableCFOptions& _mutable_cf_options,
                       std::vector<CompactionInputFiles> _inputs,
                       int _output_level, uint64_t _target_file_size,
                       uint64_t _max_grandparent_overlap_bytes,
                       uint32_t _output_path_id, CompressionType _compression,
                       std::vector<FileMetaData*> _grandparents,
                       bool _manual_compaction, double _score,
                       bool _deletion_compaction,
                       CompactionReason _compaction_reason)
    : start_level_(_inputs[0].level),
      output_level_(_output_level),
      max_output_file_size_(_target_file_size),
      max_grandparent_overlap_bytes_(_max_grandparent_overlap_bytes),
      mutable_cf_options_(_mutable_cf_options),
      input_version_(nullptr),
      number_levels_(vstorage->num_levels()),
      cfd_(nullptr),
      output_path_id_(_output_path_id),
      output_compression_(_compression),
      deletion_compaction_(_deletion_compaction),
      inputs_(std::move(_inputs)),
      grandparents_(std::move(_grandparents)),
      grandparent_index_(0),
      overlapped_bytes_(0),
      score_(_score),
      bottommost_level_(IsBottommostLevel(output_level_, vstorage, inputs_)),
      is_full_compaction_(IsFullCompaction(vstorage, inputs_)),
      is_manual_compaction_(_manual_compaction),
      compaction_reason_(_compaction_reason) {
  seen_key_.store(false, std::memory_order_release);
  MarkFilesBeingCompacted(true);

#ifndef NDEBUG
  for (size_t i = 1; i < inputs_.size(); ++i) {
    assert(inputs_[i].level > inputs_[i - 1].level);
  }
#endif

  // setup input_levels_
  {
    input_levels_.resize(num_input_levels());
    for (size_t which = 0; which < num_input_levels(); which++) {
      DoGenerateLevelFilesBrief(&input_levels_[which], inputs_[which].files,
                                &arena_);
    }
  }

  Slice smallest_user_key;
  GetBoundaryKeys(vstorage, inputs_, &smallest_user_key, &largest_user_key_);
}

Compaction::~Compaction() {
  if (input_version_ != nullptr) {
    input_version_->Unref();
  } else if (cfd_ != nullptr) {
    // If we don't hold input_version_, unref each input file separately.
    for (auto& input_level : inputs_) {
      for (auto f : input_level.files) {
        vset_->UnrefFile(cfd_, f);
      }
    }
  }
  if (cfd_ != nullptr) {
    if (cfd_->Unref()) {
      delete cfd_;
    }
  }
}

bool Compaction::InputCompressionMatchesOutput() const {
  int base_level = IsCompactionStyleUniversal()
      ? -1
      : input_version_->storage_info()->base_level();
  bool matches = (GetCompressionType(*cfd_->ioptions(), start_level_,
                                     base_level) == output_compression_);
  if (matches) {
    DEBUG_ONLY_TEST_SYNC_POINT("Compaction::InputCompressionMatchesOutput:Matches");
    return true;
  }
  DEBUG_ONLY_TEST_SYNC_POINT("Compaction::InputCompressionMatchesOutput:DidntMatch");
  return matches;
}

bool Compaction::IsTrivialMove() const {
  // Avoid a move if there is lots of overlapping grandparent data.
  // Otherwise, the move could create a parent file that will require
  // a very expensive merge later on.
  // If start_level_== output_level_, the purpose is to force compaction
  // filter to be applied to that level, and thus cannot be a trivial move.

  // Check if start level have files with overlapping ranges
  if (start_level_ == 0 && !input_version_level0_non_overlapping_) {
    // We cannot move files from L0 to L1 if the files are overlapping
    return false;
  }

  if (is_manual_compaction_ &&
      (cfd_->ioptions()->compaction_filter != nullptr ||
       cfd_->ioptions()->compaction_filter_factory != nullptr)) {
    // This is a manual compaction and we have a compaction filter that should
    // be executed, we cannot do a trivial move
    return false;
  }

  // Used in universal compaction, where trivial move can be done if the
  // input files are non overlapping
  if ((cfd_->ioptions()->compaction_options_universal.allow_trivial_move) &&
      (output_level_ != 0)) {
    return is_trivial_move_;
  }

  return (start_level_ != output_level_ && num_input_levels() == 1 &&
          input(0, 0)->fd.GetPathId() == output_path_id() &&
          InputCompressionMatchesOutput() &&
          TotalFileSize(grandparents_) <= max_grandparent_overlap_bytes_);
}

void Compaction::AddInputDeletions(VersionEdit* out_edit) {
  for (size_t which = 0; which < num_input_levels(); which++) {
    for (size_t i = 0; i < inputs_[which].size(); i++) {
      out_edit->DeleteFile(level(which), inputs_[which][i]->fd.GetNumber());
    }
  }
}

bool Compaction::KeyNotExistsBeyondOutputLevel(
    const Slice& user_key, std::vector<size_t>* level_ptrs) const {
  assert(level_ptrs != nullptr);
  assert(level_ptrs->size() == static_cast<size_t>(number_levels_));
  assert(cfd_->ioptions()->compaction_style != kCompactionStyleFIFO);
  if (IsCompactionStyleUniversal()) {
    return bottommost_level_;
  }
  DCHECK_ONLY_NOTNULL(input_version_);
  // Maybe use binary search to find right entry instead of linear search?
  const Comparator* user_cmp = cfd_->user_comparator();
  for (int lvl = output_level_ + 1; lvl < number_levels_; lvl++) {
    const std::vector<FileMetaData*>& files =
        input_version_->storage_info()->LevelFiles(lvl);
    for (; level_ptrs->at(lvl) < files.size(); level_ptrs->at(lvl)++) {
      auto* f = files[level_ptrs->at(lvl)];
      if (user_cmp->Compare(user_key, f->largest.key.user_key()) <= 0) {
        // We've advanced far enough
        if (user_cmp->Compare(user_key, f->smallest.key.user_key()) >= 0) {
          // Key falls in this file's range, so definitely
          // exists beyond output level
          return false;
        }
        break;
      }
    }
  }
  return true;
}

bool Compaction::ShouldStopBefore(const Slice& internal_key) {
  // Scan to find earliest grandparent file that contains key.
  const InternalKeyComparator* icmp = cfd_->internal_comparator().get();
  while (grandparent_index_ < grandparents_.size() &&
      icmp->Compare(internal_key,
                    grandparents_[grandparent_index_]->largest.key.Encode()) > 0) {
    if (seen_key_.load(std::memory_order_acquire)) {
      overlapped_bytes_ += grandparents_[grandparent_index_]->fd.GetTotalFileSize();
    }
    assert(grandparent_index_ + 1 >= grandparents_.size() ||
           icmp->Compare(grandparents_[grandparent_index_]->largest.key.Encode(),
                         grandparents_[grandparent_index_ + 1]->smallest.key.Encode())
                         < 0);
    grandparent_index_++;
  }
  seen_key_.store(true, std::memory_order_release);

  if (overlapped_bytes_ > max_grandparent_overlap_bytes_) {
    // Too much overlap for current output; start new output
    overlapped_bytes_ = 0;
    return true;
  } else {
    return false;
  }
}

// Mark (or clear) each file that is being compacted
void Compaction::MarkFilesBeingCompacted(bool mark_as_compacted) {
  for (size_t i = 0; i < num_input_levels(); i++) {
    for (size_t j = 0; j < inputs_[i].size(); j++) {
      assert(mark_as_compacted ? !inputs_[i][j]->being_compacted :
                                  inputs_[i][j]->being_compacted);
      inputs_[i][j]->being_compacted = mark_as_compacted;
    }
  }
}

// Sample output:
// If compacting 3 L0 files, 2 L3 files and 1 L4 file, and outputting to L5,
// print: "3@0 + 2@3 + 1@4 files to L5"
const char* Compaction::InputLevelSummary(
    InputLevelSummaryBuffer* scratch) const {
  int len = 0;
  bool is_first = true;
  for (auto& input_level : inputs_) {
    if (input_level.empty()) {
      continue;
    }
    if (!is_first) {
      len +=
          snprintf(scratch->buffer + len, sizeof(scratch->buffer) - len, " + ");
    } else {
      is_first = false;
    }
    len += snprintf(scratch->buffer + len, sizeof(scratch->buffer) - len,
                    "%" ROCKSDB_PRIszt "@%d", input_level.size(),
                    input_level.level);
  }
  snprintf(scratch->buffer + len, sizeof(scratch->buffer) - len,
           " files to L%d", output_level());

  return scratch->buffer;
}

uint64_t Compaction::CalculateTotalInputSize() const {
  uint64_t size = 0;
  for (auto& input_level : inputs_) {
    for (auto f : input_level.files) {
      size += f->fd.GetTotalFileSize();
    }
  }
  return size;
}

void Compaction::ReleaseCompactionFiles(Status status) {
  MarkFilesBeingCompacted(false);
  cfd_->compaction_picker()->ReleaseCompactionFiles(this, status);
}

void Compaction::ResetNextCompactionIndex() {
  if (!IsCompactionStyleUniversal()) {
    DCHECK_ONLY_NOTNULL(input_version_);
    input_version_->storage_info()->ResetNextCompactionIndex(start_level_);
  }
}

namespace {
int InputSummary(const std::vector<FileMetaData*>& files, char* output,
                 int len) {
  *output = '\0';
  int write = 0;
  for (size_t i = 0; i < files.size(); i++) {
    int sz = len - write;
    int ret;
    char sztxt[16];
    AppendHumanBytes(files.at(i)->fd.GetTotalFileSize(), sztxt, 16);
    ret = snprintf(output + write, sz, "%" PRIu64 "(%s) ",
                   files.at(i)->fd.GetNumber(), sztxt);
    if (ret < 0 || ret >= sz) break;
    write += ret;
  }
  // if files.size() is non-zero, overwrite the last space
  return write - !!files.size();
}
}  // namespace

void Compaction::Summary(char* output, int len) {
  int write =
      snprintf(output, len, "Base version %" PRIu64
                            " Base level %d, inputs: [",
               input_version_number_, start_level_);
  if (write < 0 || write >= len) {
    return;
  }

  for (size_t level_iter = 0; level_iter < num_input_levels(); ++level_iter) {
    if (level_iter > 0) {
      write += snprintf(output + write, len - write, "], [");
      if (write < 0 || write >= len) {
        return;
      }
    }
    write +=
        InputSummary(inputs_[level_iter].files, output + write, len - write);
    if (write < 0 || write >= len) {
      return;
    }
  }

  snprintf(output + write, len - write, "]");
}

uint64_t Compaction::OutputFilePreallocationSize() {
  uint64_t preallocation_size = 0;

  if (cfd_->ioptions()->compaction_style == kCompactionStyleLevel ||
      output_level() > 0) {
    preallocation_size = max_output_file_size_;
  } else {
    // output_level() == 0
    assert(num_input_levels() > 0);
    for (const auto& f : inputs_[0].files) {
      preallocation_size += f->fd.GetTotalFileSize();
    }
  }
  constexpr uint64_t kMaxPreAllocationSize = 1_GB;
  // Over-estimate slightly so we don't end up just barely crossing
  // the threshold
  return std::min(kMaxPreAllocationSize, preallocation_size + (preallocation_size / 10));
}

std::unique_ptr<CompactionFilter> Compaction::CreateCompactionFilter() const {
  if (!cfd_->ioptions()->compaction_filter_factory) {
    return nullptr;
  }

  CompactionFilter::Context context;
  context.is_full_compaction = is_full_compaction_;
  context.is_manual_compaction = is_manual_compaction_;
  context.column_family_id = cfd_->GetID();
  return cfd_->ioptions()->compaction_filter_factory->CreateCompactionFilter(
      context);
}

bool Compaction::IsOutputLevelEmpty() const {
  return inputs_.back().level != output_level_ || inputs_.back().empty();
}

bool Compaction::ShouldFormSubcompactions() const {
  if (mutable_cf_options_.max_subcompactions <= 1 || cfd_ == nullptr) {
    return false;
  }
  if (cfd_->ioptions()->compaction_style == kCompactionStyleLevel) {
    return start_level_ == 0 && !IsOutputLevelEmpty();
  } else if (IsCompactionStyleUniversal()) {
    return number_levels_ > 1 && output_level_ > 0;
  } else {
    return false;
  }
}

}  // namespace rocksdb
