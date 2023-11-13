// Copyright (c) YugaByte, Inc.
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
//
//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/db/compaction_picker.h"
#include "yb/rocksdb/immutable_options.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include <limits>
#include <queue>
#include <string>
#include <utility>


#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/util/log_buffer.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/statistics.h"

#include "yb/util/logging.h"
#include "yb/util/flags.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"

DEFINE_UNKNOWN_bool(aggressive_compaction_for_read_amp, false,
            "Determines if we should compact aggressively to reduce read amplification based on "
            "number of files alone, without regards to relative sizes of the SSTable files.");

namespace rocksdb {

namespace {
uint64_t TotalCompensatedFileSize(const std::vector<FileMetaData*>& files) {
  uint64_t sum = 0;
  for (size_t i = 0; i < files.size() && files[i]; i++) {
    sum += files[i]->compensated_file_size;
  }
  return sum;
}

// Used in universal compaction when trivial move is enabled.
// This structure is used for the construction of min heap
// that contains the file meta data, the level of the file
// and the index of the file in that level

struct InputFileInfo {
  InputFileInfo() : f(nullptr) {}

  FileMetaData* f;
  size_t level;
  size_t index;
};

// Used in universal compaction when trivial move is enabled.
// This comparator is used for the construction of min heap
// based on the smallest key of the file.
struct UserKeyComparator {
  explicit UserKeyComparator(const Comparator* ucmp) { ucmp_ = ucmp; }

  bool operator()(InputFileInfo i1, InputFileInfo i2) const {
    return (ucmp_->Compare(i1.f->smallest.key.user_key(),
                           i2.f->smallest.key.user_key()) > 0);
  }

 private:
  const Comparator* ucmp_;
};

typedef std::priority_queue<InputFileInfo, std::vector<InputFileInfo>,
                            UserKeyComparator> SmallestKeyHeap;
constexpr auto kFormatFileSizeInfoBufSize = 256;

// This function creates the heap that is used to find if the files are
// overlapping during universal compaction when the allow_trivial_move
// is set.
SmallestKeyHeap create_level_heap(Compaction* c, const Comparator* ucmp) {
  SmallestKeyHeap smallest_key_priority_q =
      SmallestKeyHeap(UserKeyComparator(ucmp));

  InputFileInfo input_file;

  for (size_t l = 0; l < c->num_input_levels(); l++) {
    if (c->num_input_files(l) != 0) {
      if (l == 0 && c->start_level() == 0) {
        for (size_t i = 0; i < c->num_input_files(0); i++) {
          input_file.f = c->input(0, i);
          input_file.level = 0;
          input_file.index = i;
          smallest_key_priority_q.push(std::move(input_file));
        }
      } else {
        input_file.f = c->input(l, 0);
        input_file.level = l;
        input_file.index = 0;
        smallest_key_priority_q.push(std::move(input_file));
      }
    }
  }
  return smallest_key_priority_q;
}
}  // anonymous namespace

// Determine compression type, based on user options, level of the output
// file and whether compression is disabled.
// If enable_compression is false, then compression is always disabled no
// matter what the values of the other two parameters are.
// Otherwise, the compression type is determined based on options and level.
CompressionType GetCompressionType(const ImmutableCFOptions& ioptions,
                                   int level, int base_level,
                                   const bool enable_compression) {
  if (!enable_compression) {
    // disable compression
    return kNoCompression;
  }
  // If the use has specified a different compression level for each level,
  // then pick the compression for that level.
  if (!ioptions.compression_per_level.empty()) {
    assert(level == 0 || level >= base_level);
    int idx = (level == 0) ? 0 : level - base_level + 1;

    const int n = static_cast<int>(ioptions.compression_per_level.size()) - 1;
    // It is possible for level_ to be -1; in that case, we use level
    // 0's compression.  This occurs mostly in backwards compatibility
    // situations when the builder doesn't know what level the file
    // belongs to.  Likewise, if level is beyond the end of the
    // specified compression levels, use the last value.
    return ioptions.compression_per_level[std::max(0, std::min(idx, n))];
  } else {
    return ioptions.compression;
  }
}

CompactionPicker::CompactionPicker(const ImmutableCFOptions& ioptions,
                                   const InternalKeyComparator* icmp)
    : ioptions_(ioptions), icmp_(icmp) {}

CompactionPicker::~CompactionPicker() {}

// Delete this compaction from the list of running compactions.
void CompactionPicker::ReleaseCompactionFiles(Compaction* c, Status status) {
  if (c->start_level() == 0 ||
      ioptions_.compaction_style == kCompactionStyleUniversal) {
    level0_compactions_in_progress_.erase(c);
  }
  if (!status.ok()) {
    c->ResetNextCompactionIndex();
  }
}

void CompactionPicker::GetRange(const CompactionInputFiles& inputs,
                                InternalKey* smallest, InternalKey* largest) {
  const int level = inputs.level;
  assert(!inputs.empty());
  smallest->Clear();
  largest->Clear();

  if (level == 0) {
    for (size_t i = 0; i < inputs.size(); i++) {
      FileMetaData* f = inputs[i];
      if (i == 0) {
        *smallest = f->smallest.key;
        *largest = f->largest.key;
      } else {
        if (icmp_->Compare(f->smallest.key, *smallest) < 0) {
          *smallest = f->smallest.key;
        }
        if (icmp_->Compare(f->largest.key, *largest) > 0) {
          *largest = f->largest.key;
        }
      }
    }
  } else {
    *smallest = inputs[0]->smallest.key;
    *largest = inputs[inputs.size() - 1]->largest.key;
  }
}

void CompactionPicker::GetRange(const CompactionInputFiles& inputs1,
                                const CompactionInputFiles& inputs2,
                                InternalKey* smallest, InternalKey* largest) {
  assert(!inputs1.empty() || !inputs2.empty());
  if (inputs1.empty()) {
    GetRange(inputs2, smallest, largest);
  } else if (inputs2.empty()) {
    GetRange(inputs1, smallest, largest);
  } else {
    InternalKey smallest1, smallest2, largest1, largest2;
    GetRange(inputs1, &smallest1, &largest1);
    GetRange(inputs2, &smallest2, &largest2);
    *smallest = icmp_->Compare(smallest1, smallest2) < 0 ?
                smallest1 : smallest2;
    *largest = icmp_->Compare(largest1, largest2) < 0 ?
               largest2 : largest1;
  }
}

bool CompactionPicker::ExpandWhileOverlapping(const std::string& cf_name,
                                              VersionStorageInfo* vstorage,
                                              CompactionInputFiles* inputs) {
  // This isn't good compaction
  assert(!inputs->empty());

  const int level = inputs->level;
  // GetOverlappingInputs will always do the right thing for level-0.
  // So we don't need to do any expansion if level == 0.
  if (level == 0) {
    return true;
  }

  InternalKey smallest, largest;

  // Keep expanding inputs until we are sure that there is a "clean cut"
  // boundary between the files in input and the surrounding files.
  // This will ensure that no parts of a key are lost during compaction.
  int hint_index = -1;
  size_t old_size;
  do {
    old_size = inputs->size();
    GetRange(*inputs, &smallest, &largest);
    inputs->clear();
    vstorage->GetOverlappingInputs(level, &smallest, &largest, &inputs->files,
                                   hint_index, &hint_index);
  } while (inputs->size() > old_size);

  // we started off with inputs non-empty and the previous loop only grew
  // inputs. thus, inputs should be non-empty here
  assert(!inputs->empty());

  // If, after the expansion, there are files that are already under
  // compaction, then we must drop/cancel this compaction.
  if (FilesInCompaction(inputs->files)) {
    RLOG(InfoLogLevel::WARN_LEVEL, ioptions_.info_log,
        "[%s] ExpandWhileOverlapping() failure because some of the necessary"
        " compaction input files are currently being compacted.",
        cf_name.c_str());
    return false;
  }
  return true;
}

// Returns true if any one of specified files are being compacted
bool CompactionPicker::FilesInCompaction(
    const std::vector<FileMetaData*>& files) {
  for (size_t i = 0; i < files.size(); i++) {
    if (files[i]->being_compacted) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<Compaction> CompactionPicker::FormCompaction(
    const CompactionOptions& compact_options,
    const std::vector<CompactionInputFiles>& input_files, int output_level,
    VersionStorageInfo* vstorage, const MutableCFOptions& mutable_cf_options,
    uint32_t output_path_id) const {
  uint64_t max_grandparent_overlap_bytes =
      output_level + 1 < vstorage->num_levels() ?
          mutable_cf_options.MaxGrandParentOverlapBytes(output_level + 1) :
          std::numeric_limits<uint64_t>::max();
  DCHECK_GT(input_files.size(), 0);
  return Compaction::Create(
      vstorage, mutable_cf_options, input_files, output_level,
      compact_options.output_file_size_limit, max_grandparent_overlap_bytes, output_path_id,
      compact_options.compression,
      /* grandparents = */ std::vector<FileMetaData*>(), ioptions_.info_log, true);
}

Status CompactionPicker::GetCompactionInputsFromFileNumbers(
    std::vector<CompactionInputFiles>* input_files,
    std::unordered_set<uint64_t>* input_set,
    const VersionStorageInfo* vstorage,
    const CompactionOptions& compact_options) const {
  if (input_set->size() == 0U) {
    return STATUS(InvalidArgument,
        "Compaction must include at least one file.");
  }
  assert(input_files);

  std::vector<CompactionInputFiles> matched_input_files;
  matched_input_files.resize(vstorage->num_levels());
  int first_non_empty_level = -1;
  int last_non_empty_level = -1;
  // TODO(yhchiang): use a lazy-initialized mapping from
  //                 file_number to FileMetaData in Version.
  for (int level = 0; level < vstorage->num_levels(); ++level) {
    for (auto file : vstorage->LevelFiles(level)) {
      auto iter = input_set->find(file->fd.GetNumber());
      if (iter != input_set->end()) {
        matched_input_files[level].files.push_back(file);
        input_set->erase(iter);
        last_non_empty_level = level;
        if (first_non_empty_level == -1) {
          first_non_empty_level = level;
        }
      }
    }
  }

  if (!input_set->empty()) {
    std::string message(
        "Cannot find matched SST files for the following file numbers:");
    for (auto fn : *input_set) {
      message += " ";
      message += ToString(fn);
    }
    return STATUS(InvalidArgument, message);
  }

  for (int level = first_non_empty_level;
       level <= last_non_empty_level; ++level) {
    matched_input_files[level].level = level;
    input_files->emplace_back(std::move(matched_input_files[level]));
  }

  return Status::OK();
}



// Returns true if any one of the parent files are being compacted
bool CompactionPicker::RangeInCompaction(VersionStorageInfo* vstorage,
                                         const InternalKey* smallest,
                                         const InternalKey* largest,
                                         int level,
                                         int* level_index) {
  std::vector<FileMetaData*> inputs;
  assert(level < NumberLevels());

  vstorage->GetOverlappingInputs(level, smallest, largest, &inputs,
                                 *level_index, level_index);
  return FilesInCompaction(inputs);
}

// Populates the set of inputs of all other levels that overlap with the
// start level.
// Now we assume all levels except start level and output level are empty.
// Will also attempt to expand "start level" if that doesn't expand
// "output level" or cause "level" to include a file for compaction that has an
// overlapping user-key with another file.
// REQUIRES: input_level and output_level are different
// REQUIRES: inputs->empty() == false
// Returns false if files on parent level are currently in compaction, which
// means that we can't compact them
bool CompactionPicker::SetupOtherInputs(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, CompactionInputFiles* inputs,
    CompactionInputFiles* output_level_inputs, int* parent_index,
    int base_index) {
  assert(!inputs->empty());
  assert(output_level_inputs->empty());
  const int input_level = inputs->level;
  const int output_level = output_level_inputs->level;
  assert(input_level != output_level);

  // For now, we only support merging two levels, start level and output level.
  // We need to assert other levels are empty.
  for (int l = input_level + 1; l < output_level; l++) {
    assert(vstorage->NumLevelFiles(l) == 0);
  }

  InternalKey smallest, largest;

  // Get the range one last time.
  GetRange(*inputs, &smallest, &largest);

  // Populate the set of next-level files (inputs_GetOutputLevelInputs()) to
  // include in compaction
  vstorage->GetOverlappingInputs(output_level, &smallest, &largest,
                                 &output_level_inputs->files, *parent_index,
                                 parent_index);

  if (FilesInCompaction(output_level_inputs->files)) {
    return false;
  }

  // See if we can further grow the number of inputs in "level" without
  // changing the number of "level+1" files we pick up. We also choose NOT
  // to expand if this would cause "level" to include some entries for some
  // user key, while excluding other entries for the same user key. This
  // can happen when one user key spans multiple files.
  if (!output_level_inputs->empty()) {
    CompactionInputFiles expanded0;
    expanded0.level = input_level;
    // Get entire range covered by compaction
    InternalKey all_start, all_limit;
    GetRange(*inputs, *output_level_inputs, &all_start, &all_limit);

    vstorage->GetOverlappingInputs(input_level, &all_start, &all_limit,
                                   &expanded0.files, base_index, nullptr);
    const uint64_t inputs0_size = TotalCompensatedFileSize(inputs->files);
    const uint64_t inputs1_size =
        TotalCompensatedFileSize(output_level_inputs->files);
    const uint64_t expanded0_size = TotalCompensatedFileSize(expanded0.files);
    uint64_t limit =
        mutable_cf_options.ExpandedCompactionByteSizeLimit(input_level);
    if (expanded0.size() > inputs->size() &&
        inputs1_size + expanded0_size < limit &&
        !FilesInCompaction(expanded0.files) &&
        !vstorage->HasOverlappingUserKey(&expanded0.files, input_level)) {
      InternalKey new_start, new_limit;
      GetRange(expanded0, &new_start, &new_limit);
      std::vector<FileMetaData*> expanded1;
      vstorage->GetOverlappingInputs(output_level, &new_start, &new_limit,
                                     &expanded1, *parent_index, parent_index);
      if (expanded1.size() == output_level_inputs->size() &&
          !FilesInCompaction(expanded1)) {
        RLOG(InfoLogLevel::INFO_LEVEL, ioptions_.info_log,
            "[%s] Expanding@%d %" ROCKSDB_PRIszt "+%" ROCKSDB_PRIszt "(%" PRIu64
            "+%" PRIu64 " bytes) to %" ROCKSDB_PRIszt "+%" ROCKSDB_PRIszt
            " (%" PRIu64 "+%" PRIu64 "bytes)\n",
            cf_name.c_str(), input_level, inputs->size(),
            output_level_inputs->size(), inputs0_size, inputs1_size,
            expanded0.size(), expanded1.size(), expanded0_size, inputs1_size);
        smallest = new_start;
        largest = new_limit;
        inputs->files = expanded0.files;
        output_level_inputs->files = expanded1;
      }
    }
  }

  return true;
}

void CompactionPicker::GetGrandparents(
    VersionStorageInfo* vstorage, const CompactionInputFiles& inputs,
    const CompactionInputFiles& output_level_inputs,
    std::vector<FileMetaData*>* grandparents) {
  InternalKey start, limit;
  GetRange(inputs, output_level_inputs, &start, &limit);
  // Compute the set of grandparent files that overlap this compaction
  // (parent == level+1; grandparent == level+2)
  if (output_level_inputs.level + 1 < NumberLevels()) {
    vstorage->GetOverlappingInputs(output_level_inputs.level + 1, &start,
                                   &limit, grandparents);
  }
}

void CompactionPicker::MarkL0FilesForDeletion(
    const VersionStorageInfo* vstorage,
    const ImmutableCFOptions* ioptions) {
  // CompactionFileFilterFactory is used to determine files that can be directly removed during
  // compaction rather than requiring a full iteration through the files.
  if (!ioptions->compaction_file_filter_factory) {
    return;
  }
  // Compaction file filter factory should only look at files in Level 0.
  auto file_filter = ioptions->compaction_file_filter_factory->CreateCompactionFileFilter(
      vstorage->LevelFiles(0));
  for (FileMetaData* f : vstorage->LevelFiles(0)) {
    if (file_filter && file_filter->Filter(f) == FilterDecision::kDiscard) {
      f->set_delete_after_compaction(true);
    }
  }
}

std::unique_ptr<Compaction> CompactionPicker::CompactRange(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, int input_level, int output_level,
    uint32_t output_path_id, const InternalKey* begin, const InternalKey* end,
    CompactionReason compaction_reason, InternalKey** compaction_end,
    bool* manual_conflict) {
  // CompactionPickerFIFO has its own implementation of compact range
  assert(ioptions_.compaction_style != kCompactionStyleFIFO);

  if (input_level == ColumnFamilyData::kCompactAllLevels) {
    assert(ioptions_.compaction_style == kCompactionStyleUniversal);

    // Universal compaction with more than one level always compacts all the
    // files together to the last level.
    assert(vstorage->num_levels() > 1);
    // DBImpl::CompactRange() set output level to be the last level
    assert(output_level == vstorage->num_levels() - 1);
    // DBImpl::RunManualCompaction will make full range for universal compaction
    assert(begin == nullptr);
    assert(end == nullptr);
    *compaction_end = nullptr;

    int start_level = 0;
    for (; start_level < vstorage->num_levels() &&
           vstorage->NumLevelFiles(start_level) == 0;
         start_level++) {
    }
    if (start_level == vstorage->num_levels()) {
      return nullptr;
    }

    if ((start_level == 0) && (!level0_compactions_in_progress_.empty())) {
      *manual_conflict = true;
      // Only one level 0 compaction allowed
      return nullptr;
    }

    std::vector<CompactionInputFiles> inputs(vstorage->num_levels() -
                                             start_level);
    for (int level = start_level; level < vstorage->num_levels(); level++) {
      inputs[level - start_level].level = level;
      auto& files = inputs[level - start_level].files;

      for (FileMetaData* f : vstorage->LevelFiles(level)) {
        files.push_back(f);
      }
      if (FilesInCompaction(files)) {
        *manual_conflict = true;
        return nullptr;
      }
    }
    auto c = Compaction::Create(
        vstorage, mutable_cf_options, std::move(inputs), output_level,
        mutable_cf_options.MaxFileSizeForLevel(output_level),
        /* max_grandparent_overlap_bytes = */ LLONG_MAX, output_path_id,
        GetCompressionType(ioptions_, output_level, 1),
        /* grandparents = */ std::vector<FileMetaData*>(), ioptions_.info_log,
        /* is_manual = */ true, /* score */ -1, /* deletion_compaction */ false,
        compaction_reason);
    if (c && start_level == 0) {
      MarkL0FilesForDeletion(vstorage, &ioptions_);
      level0_compactions_in_progress_.insert(c.get());
    }
    return c;
  }

  CompactionInputFiles inputs;
  inputs.level = input_level;
  bool covering_the_whole_range = true;

  // All files are 'overlapping' in universal style compaction.
  // We have to compact the entire range in one shot.
  if (ioptions_.compaction_style == kCompactionStyleUniversal) {
    begin = nullptr;
    end = nullptr;
  }

  vstorage->GetOverlappingInputs(input_level, begin, end, &inputs.files);
  if (inputs.empty()) {
    return nullptr;
  }

  if ((input_level == 0) && (!level0_compactions_in_progress_.empty())) {
    // Only one level 0 compaction allowed
    TEST_SYNC_POINT("CompactionPicker::CompactRange:Conflict");
    *manual_conflict = true;
    return nullptr;
  }

  // Avoid compacting too much in one shot in case the range is large.
  // But we cannot do this for level-0 since level-0 files can overlap
  // and we must not pick one file and drop another older file if the
  // two files overlap.
  if (input_level > 0) {
    const uint64_t limit = mutable_cf_options.MaxFileSizeForLevel(input_level) *
      mutable_cf_options.source_compaction_factor;
    uint64_t total = 0;
    for (size_t i = 0; i + 1 < inputs.size(); ++i) {
      uint64_t s = inputs[i]->compensated_file_size;
      total += s;
      if (total >= limit) {
        **compaction_end = inputs[i + 1]->smallest.key;
        covering_the_whole_range = false;
        inputs.files.resize(i + 1);
        break;
      }
    }
  }
  assert(output_path_id < static_cast<uint32_t>(ioptions_.db_paths.size()));

  if (ExpandWhileOverlapping(cf_name, vstorage, &inputs) == false) {
    // manual compaction is now multi-threaded, so it can
    // happen that ExpandWhileOverlapping fails
    // we handle it higher in RunManualCompaction
    *manual_conflict = true;
    return nullptr;
  }

  if (covering_the_whole_range) {
    *compaction_end = nullptr;
  }

  CompactionInputFiles output_level_inputs;
  if (output_level == ColumnFamilyData::kCompactToBaseLevel) {
    assert(input_level == 0);
    output_level = vstorage->base_level();
    assert(output_level > 0);
  }
  output_level_inputs.level = output_level;
  if (input_level != output_level) {
    int parent_index = -1;
    if (!SetupOtherInputs(cf_name, mutable_cf_options, vstorage, &inputs,
                          &output_level_inputs, &parent_index, -1)) {
      // manual compaction is now multi-threaded, so it can
      // happen that SetupOtherInputs fails
      // we handle it higher in RunManualCompaction
      *manual_conflict = true;
      return nullptr;
    }
  }

  std::vector<CompactionInputFiles> compaction_inputs({inputs});
  if (!output_level_inputs.empty()) {
    compaction_inputs.push_back(output_level_inputs);
  }
  for (size_t i = 0; i < compaction_inputs.size(); i++) {
    if (FilesInCompaction(compaction_inputs[i].files)) {
      *manual_conflict = true;
      return nullptr;
    }
  }

  std::vector<FileMetaData*> grandparents;
  GetGrandparents(vstorage, inputs, output_level_inputs, &grandparents);
  auto compaction = Compaction::Create(
      vstorage, mutable_cf_options, std::move(compaction_inputs), output_level,
      mutable_cf_options.MaxFileSizeForLevel(output_level),
      mutable_cf_options.MaxGrandParentOverlapBytes(input_level), output_path_id,
      GetCompressionType(ioptions_, output_level, vstorage->base_level()), std::move(grandparents),
      ioptions_.info_log, /* is manual compaction = */ true, /* score */ -1,
      /* deletion_compaction */ false, compaction_reason);
  if (!compaction) {
    return nullptr;
  }

  TEST_SYNC_POINT_CALLBACK("CompactionPicker::CompactRange:Return", compaction.get());
  if (input_level == 0) {
    MarkL0FilesForDeletion(vstorage, &ioptions_);
    level0_compactions_in_progress_.insert(compaction.get());
  }

  // Creating a compaction influences the compaction score because the score
  // takes running compactions into account (by skipping files that are already
  // being compacted). Since we just changed compaction score, we recalculate it
  // here
  {  // this piece of code recomputes compaction score
    CompactionOptionsFIFO dummy_compaction_options_fifo;
    vstorage->ComputeCompactionScore(mutable_cf_options,
                                     dummy_compaction_options_fifo);
  }

  return compaction;
}

// Test whether two files have overlapping key-ranges.
bool HaveOverlappingKeyRanges(const Comparator* c,
                              const SstFileMetaData::BoundaryValues& a_smallest,
                              const SstFileMetaData::BoundaryValues& a_largest,
                              const SstFileMetaData::BoundaryValues& b_smallest,
                              const SstFileMetaData::BoundaryValues& b_largest) {
  return c->Compare(a_largest.key, b_smallest.key) >= 0 &&
         c->Compare(b_largest.key, a_smallest.key) >= 0;
}

bool HaveOverlappingKeyRanges(const Comparator* c,
                              const SstFileMetaData::BoundaryValues& a_smallest,
                              const SstFileMetaData::BoundaryValues& a_largest,
                              const SstFileMetaData& b) {
  return HaveOverlappingKeyRanges(c, a_smallest, a_largest, b.smallest, b.largest);
}

bool HaveOverlappingKeyRanges(
    const Comparator* c, const SstFileMetaData& a, const SstFileMetaData& b) {
  return HaveOverlappingKeyRanges(c, a.smallest, a.largest, b.smallest, b.largest);
}

namespace {

// Updates smallest/largest keys using keys from specified file.
void UpdateBoundaryKeys(const Comparator* comparator,
                        const SstFileMetaData& file,
                        SstFileMetaData::BoundaryValues* smallest,
                        SstFileMetaData::BoundaryValues* largest) {
  if (smallest != nullptr && comparator->Compare(smallest->key, file.smallest.key) > 0) {
    smallest->key = file.smallest.key;
  }
  if (largest != nullptr && comparator->Compare(largest->key, file.largest.key) < 0) {
    largest->key = file.largest.key;
  }
}

}  // namespace

Status CompactionPicker::SanitizeCompactionInputFilesForAllLevels(
      std::unordered_set<uint64_t>* input_files,
      const ColumnFamilyMetaData& cf_meta,
      const int output_level) const {
  auto& levels = cf_meta.levels;
  auto comparator = icmp_->user_comparator();

  // TODO(yhchiang): If there is any input files of L1 or up and there
  // is at least one L0 files. All L0 files older than the L0 file needs
  // to be included. Otherwise, it is a false conditoin

  // TODO(yhchiang): add is_adjustable to CompactionOptions

  // the smallest and largest key of the current compaction input
  SstFileMetaData::BoundaryValues smallest, largest;
  // a flag for initializing smallest and largest key
  bool is_first = true;
  const int kNotFound = -1;

  // For each level, it does the following things:
  // 1. Find the first and the last compaction input files
  //    in the current level.
  // 2. Include all files between the first and the last
  //    compaction input files.
  // 3. Update the compaction key-range.
  // 4. For all remaining levels, include files that have
  //    overlapping key-range with the compaction key-range.
  for (int l = 0; l <= output_level; ++l) {
    auto& current_files = levels[l].files;
    int first_included = static_cast<int>(current_files.size());
    int last_included = kNotFound;

    // identify the first and the last compaction input files
    // in the current level.
    for (size_t f = 0; f < current_files.size(); ++f) {
      if (input_files->count(current_files[f].name_id)) {
        first_included = std::min(first_included, static_cast<int>(f));
        last_included = std::max(last_included, static_cast<int>(f));
        if (is_first) {
          smallest = current_files[f].smallest;
          largest = current_files[f].largest;
          is_first = false;
        }
      }
    }
    if (last_included == kNotFound) {
      continue;
    }

    if (l != 0) {
      // expend the compaction input of the current level if it
      // has overlapping key-range with other non-compaction input
      // files in the same level.
      while (first_included > 0) {
        if (comparator->Compare(
                current_files[first_included - 1].largest.key,
                current_files[first_included].smallest.key) < 0) {
          break;
        }
        first_included--;
      }

      while (last_included < static_cast<int>(current_files.size()) - 1) {
        if (comparator->Compare(
                current_files[last_included + 1].smallest.key,
                current_files[last_included].largest.key) > 0) {
          break;
        }
        last_included++;
      }
    }

    // include all files between the first and the last compaction input files.
    for (int f = first_included; f <= last_included; ++f) {
      if (current_files[f].being_compacted) {
        return STATUS(Aborted,
            "Necessary compaction input file " + current_files[f].Name() +
            " is currently being compacted.");
      }
      input_files->insert(current_files[f].name_id);
    }

    // update smallest and largest key
    if (l == 0) {
      for (int f = first_included; f <= last_included; ++f) {
        UpdateBoundaryKeys(comparator, current_files[f], &smallest, &largest);
      }
    } else {
      UpdateBoundaryKeys(comparator, current_files[first_included], &smallest, nullptr);
      UpdateBoundaryKeys(comparator, current_files[last_included], nullptr, &largest);
    }

    // For all lower levels, include all overlapping files.
    // We need to add overlapping files from the current level too because even
    // if there no input_files in level l, we would still need to add files
    // which overlap with the range containing the input_files in levels 0 to l
    // Level 0 doesn't need to be handled this way because files are sorted by
    // time and not by key
    for (int m = std::max(l, 1); m <= output_level; ++m) {
      for (auto& next_lv_file : levels[m].files) {
        if (HaveOverlappingKeyRanges(
            comparator, smallest, largest, next_lv_file)) {
          if (next_lv_file.being_compacted) {
            return STATUS(Aborted,
                "File " + next_lv_file.Name() +
                " that has overlapping key range with one of the compaction "
                " input file is currently being compacted.");
          }
          input_files->insert(next_lv_file.name_id);
        }
      }
    }
  }
  return Status::OK();
}

Status CompactionPicker::SanitizeCompactionInputFiles(
    std::unordered_set<uint64_t>* input_files,
    const ColumnFamilyMetaData& cf_meta,
    const int output_level) const {
  assert(static_cast<int>(cf_meta.levels.size()) - 1 ==
         cf_meta.levels[cf_meta.levels.size() - 1].level);
  if (output_level >= static_cast<int>(cf_meta.levels.size())) {
    return STATUS(InvalidArgument,
        "Output level for column family " + cf_meta.name +
        " must between [0, " +
        ToString(cf_meta.levels[cf_meta.levels.size() - 1].level) +
        "].");
  }

  if (output_level > MaxOutputLevel()) {
    return STATUS(InvalidArgument,
        "Exceed the maximum output level defined by "
        "the current compaction algorithm --- " +
            ToString(MaxOutputLevel()));
  }

  if (output_level < 0) {
    return STATUS(InvalidArgument,
        "Output level cannot be negative.");
  }

  if (input_files->size() == 0) {
    return STATUS(InvalidArgument,
        "A compaction must contain at least one file.");
  }

  Status s = SanitizeCompactionInputFilesForAllLevels(
      input_files, cf_meta, output_level);

  if (!s.ok()) {
    return s;
  }

  // for all input files, check whether the file number matches
  // any currently-existing files.
  for (auto file_num : *input_files) {
    bool found = false;
    for (auto level_meta : cf_meta.levels) {
      for (auto file_meta : level_meta.files) {
        if (file_num == file_meta.name_id) {
          if (file_meta.being_compacted) {
            return STATUS(Aborted,
                "Specified compaction input file " +
                MakeTableFileName("", file_num) +
                " is already being compacted.");
          }
          found = true;
          break;
        }
      }
      if (found) {
        break;
      }
    }
    if (!found) {
      return STATUS(InvalidArgument,
          "Specified compaction input file " +
          MakeTableFileName("", file_num) +
          " does not exist in column family " + cf_meta.name + ".");
    }
  }

  return Status::OK();
}

bool LevelCompactionPicker::NeedsCompaction(const VersionStorageInfo* vstorage)
    const {
  if (!vstorage->FilesMarkedForCompaction().empty()) {
    return true;
  }
  for (int i = 0; i <= vstorage->MaxInputLevel(); i++) {
    if (vstorage->CompactionScore(i) >= 1) {
      return true;
    }
  }
  return false;
}

void LevelCompactionPicker::PickFilesMarkedForCompactionExperimental(
    const std::string& cf_name, VersionStorageInfo* vstorage,
    CompactionInputFiles* inputs, int* level, int* output_level) {
  if (vstorage->FilesMarkedForCompaction().empty()) {
    return;
  }

  auto continuation = [&](std::pair<int, FileMetaData*> level_file) {
    // If it's being compacted it has nothing to do here.
    // If this assert() fails that means that some function marked some
    // files as being_compacted, but didn't call ComputeCompactionScore()
    assert(!level_file.second->being_compacted);
    *level = level_file.first;
    *output_level = (*level == 0) ? vstorage->base_level() : *level + 1;

    if (*level == 0 && !level0_compactions_in_progress_.empty()) {
      return false;
    }

    inputs->files = {level_file.second};
    inputs->level = *level;
    return ExpandWhileOverlapping(cf_name, vstorage, inputs);
  };

  // take a chance on a random file first
  Random64 rnd(/* seed */ reinterpret_cast<uint64_t>(vstorage));
  size_t random_file_index = static_cast<size_t>(rnd.Uniform(
      static_cast<uint64_t>(vstorage->FilesMarkedForCompaction().size())));

  if (continuation(vstorage->FilesMarkedForCompaction()[random_file_index])) {
    // found the compaction!
    return;
  }

  for (auto& level_file : vstorage->FilesMarkedForCompaction()) {
    if (continuation(level_file)) {
      // found the compaction!
      return;
    }
  }
  inputs->files.clear();
}

std::unique_ptr<Compaction> LevelCompactionPicker::PickCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, LogBuffer* log_buffer) {
  int level = -1;
  int output_level = -1;
  int parent_index = -1;
  int base_index = -1;
  CompactionInputFiles inputs;
  double score = 0;
  CompactionReason compaction_reason = CompactionReason::kUnknown;

  // Find the compactions by size on all levels.
  bool skipped_l0 = false;
  for (int i = 0; i < NumberLevels() - 1; i++) {
    score = vstorage->CompactionScore(i);
    level = vstorage->CompactionScoreLevel(i);
    assert(i == 0 || score <= vstorage->CompactionScore(i - 1));
    if (score >= 1) {
      if (skipped_l0 && level == vstorage->base_level()) {
        // If L0->base_level compaction is pending, don't schedule further
        // compaction from base level. Otherwise L0->base_level compaction
        // may starve.
        continue;
      }
      output_level = (level == 0) ? vstorage->base_level() : level + 1;
      if (PickCompactionBySize(vstorage, level, output_level, &inputs,
                               &parent_index, &base_index) &&
          ExpandWhileOverlapping(cf_name, vstorage, &inputs)) {
        // found the compaction!
        if (level == 0) {
          // L0 score = `num L0 files` / `level0_file_num_compaction_trigger`
          compaction_reason = CompactionReason::kLevelL0FilesNum;
        } else {
          // L1+ score = `Level files size` / `MaxBytesForLevel`
          compaction_reason = CompactionReason::kLevelMaxLevelSize;
        }
        break;
      } else {
        // didn't find the compaction, clear the inputs
        inputs.clear();
        if (level == 0) {
          skipped_l0 = true;
        }
      }
    }
  }

  bool is_manual = false;
  // if we didn't find a compaction, check if there are any files marked for
  // compaction
  if (inputs.empty()) {
    is_manual = true;
    parent_index = base_index = -1;
    PickFilesMarkedForCompactionExperimental(cf_name, vstorage, &inputs, &level,
                                             &output_level);
    if (!inputs.empty()) {
      compaction_reason = CompactionReason::kFilesMarkedForCompaction;
    }
  }
  if (inputs.empty()) {
    return nullptr;
  }
  assert(level >= 0 && output_level >= 0);

  // Two level 0 compaction won't run at the same time, so don't need to worry
  // about files on level 0 being compacted.
  if (level == 0) {
    assert(level0_compactions_in_progress_.empty());
    InternalKey smallest, largest;
    GetRange(inputs, &smallest, &largest);
    // Note that the next call will discard the file we placed in
    // c->inputs_[0] earlier and replace it with an overlapping set
    // which will include the picked file.
    inputs.files.clear();
    vstorage->GetOverlappingInputs(0, &smallest, &largest, &inputs.files);

    // If we include more L0 files in the same compaction run it can
    // cause the 'smallest' and 'largest' key to get extended to a
    // larger range. So, re-invoke GetRange to get the new key range
    GetRange(inputs, &smallest, &largest);
    if (RangeInCompaction(vstorage, &smallest, &largest, output_level,
                          &parent_index)) {
      return nullptr;
    }
    assert(!inputs.files.empty());
  }

  // Setup input files from output level
  CompactionInputFiles output_level_inputs;
  output_level_inputs.level = output_level;
  if (!SetupOtherInputs(cf_name, mutable_cf_options, vstorage, &inputs,
                   &output_level_inputs, &parent_index, base_index)) {
    return nullptr;
  }

  std::vector<CompactionInputFiles> compaction_inputs({inputs});
  if (!output_level_inputs.empty()) {
    compaction_inputs.push_back(output_level_inputs);
  }

  std::vector<FileMetaData*> grandparents;
  GetGrandparents(vstorage, inputs, output_level_inputs, &grandparents);
  auto c = Compaction::Create(
      vstorage, mutable_cf_options, std::move(compaction_inputs), output_level,
      mutable_cf_options.MaxFileSizeForLevel(output_level),
      mutable_cf_options.MaxGrandParentOverlapBytes(level),
      GetPathId(ioptions_, mutable_cf_options, output_level),
      GetCompressionType(ioptions_, output_level, vstorage->base_level()), std::move(grandparents),
      ioptions_.info_log, is_manual, score,
      /* deletion_compaction = */ false, compaction_reason);
  if (!c) {
    return nullptr;
  }

  // If it's level 0 compaction, make sure we don't execute any other level 0
  // compactions in parallel
  if (level == 0) {
    level0_compactions_in_progress_.insert(c.get());
  }

  // Creating a compaction influences the compaction score because the score
  // takes running compactions into account (by skipping files that are already
  // being compacted). Since we just changed compaction score, we recalculate it
  // here
  {  // this piece of code recomputes compaction score
    CompactionOptionsFIFO dummy_compaction_options_fifo;
    vstorage->ComputeCompactionScore(mutable_cf_options,
                                     dummy_compaction_options_fifo);
  }

  TEST_SYNC_POINT_CALLBACK("LevelCompactionPicker::PickCompaction:Return", c.get());

  return c;
}

/*
 * Find the optimal path to place a file
 * Given a level, finds the path where levels up to it will fit in levels
 * up to and including this path
 */
uint32_t LevelCompactionPicker::GetPathId(
    const ImmutableCFOptions& ioptions,
    const MutableCFOptions& mutable_cf_options, int level) {
  uint32_t p = 0;
  assert(!ioptions.db_paths.empty());

  // size remaining in the most recent path
  uint64_t current_path_size = ioptions.db_paths[0].target_size;

  uint64_t level_size;
  int cur_level = 0;

  level_size = mutable_cf_options.max_bytes_for_level_base;

  // Last path is the fallback
  while (p < ioptions.db_paths.size() - 1) {
    if (level_size <= current_path_size) {
      if (cur_level == level) {
        // Does desired level fit in this path?
        return p;
      } else {
        current_path_size -= level_size;
        level_size *= mutable_cf_options.max_bytes_for_level_multiplier;
        cur_level++;
        continue;
      }
    }
    p++;
    current_path_size = ioptions.db_paths[p].target_size;
  }
  return p;
}

bool LevelCompactionPicker::PickCompactionBySize(VersionStorageInfo* vstorage,
                                                 int level, int output_level,
                                                 CompactionInputFiles* inputs,
                                                 int* parent_index,
                                                 int* base_index) {
  // level 0 files are overlapping. So we cannot pick more
  // than one concurrent compactions at this level. This
  // could be made better by looking at key-ranges that are
  // being compacted at level 0.
  if (level == 0 && !level0_compactions_in_progress_.empty()) {
    TEST_SYNC_POINT("LevelCompactionPicker::PickCompactionBySize:0");
    return false;
  }

  inputs->clear();

  assert(level >= 0);

  // Pick the largest file in this level that is not already
  // being compacted
  const std::vector<int>& file_size = vstorage->FilesByCompactionPri(level);
  const std::vector<FileMetaData*>& level_files = vstorage->LevelFiles(level);

  // record the first file that is not yet compacted
  int nextIndex = -1;

  for (unsigned int i = vstorage->NextCompactionIndex(level);
       i < file_size.size(); i++) {
    int index = file_size[i];
    auto* f = level_files[index];

    // do not pick a file to compact if it is being compacted
    // from n-1 level.
    if (f->being_compacted) {
      continue;
    }

    // remember the startIndex for the next call to PickCompaction
    if (nextIndex == -1) {
      nextIndex = i;
    }

    // Do not pick this file if its parents at level+1 are being compacted.
    // Maybe we can avoid redoing this work in SetupOtherInputs
    *parent_index = -1;
    if (RangeInCompaction(vstorage, &f->smallest.key, &f->largest.key, output_level,
                          parent_index)) {
      continue;
    }
    inputs->files.push_back(f);
    inputs->level = level;
    *base_index = index;
    break;
  }

  // store where to start the iteration in the next call to PickCompaction
  vstorage->SetNextCompactionIndex(level, nextIndex);

  return inputs->size() > 0;
}

bool UniversalCompactionPicker::NeedsCompaction(
    const VersionStorageInfo* vstorage) const {
  const int kLevel0 = 0;
  return vstorage->CompactionScore(kLevel0) >= 1;
}

struct UniversalCompactionPicker::SortedRun {
  SortedRun(int _level, FileMetaData* _file, uint64_t _size,
            uint64_t _compensated_file_size, bool _being_compacted)
      : level(_level),
        file(_file),
        size(_size),
        compensated_file_size(_compensated_file_size),
        being_compacted(_being_compacted) {
    assert(compensated_file_size > 0);
    // Allowed either one of level and file.
    assert((level != 0) != (file != nullptr));
  }

  void Dump(char* out_buf, size_t out_buf_size,
            bool print_path = false) const;

  // sorted_run_count is added into the string to print
  void DumpSizeInfo(char* out_buf, size_t out_buf_size,
                    size_t sorted_run_count) const;

  std::string FileNumAsString() const {
    char file_num_buf[kFormatFileNumberBufSize];
    Dump(file_num_buf, sizeof(file_num_buf), true);
    return file_num_buf;
  }

  bool delete_after_compaction() const {
    return file ? file->delete_after_compaction() : false;
  }

  int level;
  // `file` Will be null for level > 0. For level = 0, the sorted run is
  // for this file.
  FileMetaData* file;
  // For level > 0, `size` and `compensated_file_size` are sum of sizes all
  // files in the level. `being_compacted` should be the same for all files
  // in a non-zero level. Use the value here.
  uint64_t size;
  uint64_t compensated_file_size;
  bool being_compacted;
};

void UniversalCompactionPicker::SortedRun::Dump(char* out_buf,
                                                size_t out_buf_size,
                                                bool print_path) const {
  if (level == 0) {
    assert(file != nullptr);
    if (file->fd.GetPathId() == 0 || !print_path) {
      snprintf(out_buf, out_buf_size, "file %" PRIu64, file->fd.GetNumber());
    } else {
      snprintf(out_buf, out_buf_size, "file %" PRIu64
                                      "(path "
                                      "%" PRIu32 ")",
               file->fd.GetNumber(), file->fd.GetPathId());
    }
  } else {
    snprintf(out_buf, out_buf_size, "level %d", level);
  }
}

void UniversalCompactionPicker::SortedRun::DumpSizeInfo(
    char* out_buf, size_t out_buf_size, size_t sorted_run_count) const {
  if (level == 0) {
    assert(file != nullptr);
    snprintf(out_buf, out_buf_size,
             "file %" PRIu64 "[%" ROCKSDB_PRIszt
             "] "
             "with size %" PRIu64 " (compensated size %" PRIu64 ")",
             file->fd.GetNumber(), sorted_run_count, file->fd.GetTotalFileSize(),
             file->compensated_file_size);
  } else {
    snprintf(out_buf, out_buf_size,
             "level %d[%" ROCKSDB_PRIszt
             "] "
             "with size %" PRIu64 " (compensated size %" PRIu64 ")",
             level, sorted_run_count, size, compensated_file_size);
  }
}

std::vector<std::vector<UniversalCompactionPicker::SortedRun>>
    UniversalCompactionPicker::CalculateSortedRuns(const VersionStorageInfo& vstorage,
                                                   const ImmutableCFOptions& ioptions,
                                                   uint64_t max_file_size) {
  std::vector<std::vector<SortedRun>> ret(1);
  MarkL0FilesForDeletion(&vstorage, &ioptions);

  for (FileMetaData* f : vstorage.LevelFiles(0)) {
    // Any files that can be directly removed during compaction can be included, even if they
    // exceed the "max file size for compaction."
    if (f->fd.GetTotalFileSize() <= max_file_size || f->delete_after_compaction()) {
      ret.back().emplace_back(0, f, f->fd.GetTotalFileSize(), f->compensated_file_size,
          f->being_compacted);
    // If last sequence is empty it means that there are multiple too-large-to-compact files in
    // a row. So we just don't start new sequence in this case.
    } else if (!ret.back().empty()) {
      ret.emplace_back();
    }
  }

  for (int level = 1; level < vstorage.num_levels(); level++) {
    uint64_t total_compensated_size = 0U;
    uint64_t total_size = 0U;
    bool being_compacted = false;
    bool is_first = true;
    for (FileMetaData* f : vstorage.LevelFiles(level)) {
      total_compensated_size += f->compensated_file_size;
      total_size += f->fd.GetTotalFileSize();
      if (ioptions.compaction_options_universal.allow_trivial_move == true) {
        if (f->being_compacted) {
          being_compacted = f->being_compacted;
        }
      } else {
        // Compaction always includes all files for a non-zero level, so for a
        // non-zero level, all the files should share the same being_compacted
        // value.
        // This assumption is only valid when
        // ioptions.compaction_options_universal.allow_trivial_move is false
        assert(is_first || f->being_compacted == being_compacted);
      }
      if (is_first) {
        being_compacted = f->being_compacted;
        is_first = false;
      }
    }
    if (total_compensated_size > 0) {
      ret.back().emplace_back(level, nullptr, total_size, total_compensated_size, being_compacted);
    }
  }

  // If last sequence is empty, it means that we don't have files after too-large-to-compact file.
  // So just drop this sequence.
  if (ret.back().empty())
    ret.pop_back();
  return ret;
}

#ifndef NDEBUG
namespace {
// smallest_seqno and largest_seqno are set iff. `files` is not empty.
void GetSmallestLargestSeqno(const std::vector<FileMetaData*>& files,
                             SequenceNumber* smallest_seqno,
                             SequenceNumber* largest_seqno) {
  DCHECK_ONLY_NOTNULL(smallest_seqno);
  DCHECK_ONLY_NOTNULL(largest_seqno);
  bool is_first = true;
  for (FileMetaData* f : files) {
    DCHECK_LE(f->smallest.seqno, f->largest.seqno);
    if (is_first) {
      is_first = false;
      *smallest_seqno = f->smallest.seqno;
      *largest_seqno = f->largest.seqno;
    } else {
      if (f->smallest.seqno < *smallest_seqno) {
        *smallest_seqno = f->smallest.seqno;
      }
      if (f->largest.seqno > *largest_seqno) {
        *largest_seqno = f->largest.seqno;
      }
    }
  }
}
}  // namespace
#endif

// Algorithm that checks to see if there are any overlapping
// files in the input
bool CompactionPicker::IsInputNonOverlapping(Compaction* c) {
  auto comparator = icmp_->user_comparator();
  int first_iter = 1;

  InputFileInfo prev, curr, next;

  SmallestKeyHeap smallest_key_priority_q =
      create_level_heap(c, icmp_->user_comparator());

  while (!smallest_key_priority_q.empty()) {
    curr = smallest_key_priority_q.top();
    smallest_key_priority_q.pop();

    if (first_iter) {
      prev = curr;
      first_iter = 0;
    } else {
      if (comparator->Compare(prev.f->largest.key.user_key(),
                              curr.f->smallest.key.user_key()) >= 0) {
        // found overlapping files, return false
        return false;
      }
      assert(comparator->Compare(curr.f->largest.key.user_key(),
                                 prev.f->largest.key.user_key()) > 0);
      prev = curr;
    }

    next.f = nullptr;

    if (curr.level != 0 && curr.index < c->num_input_files(curr.level) - 1) {
      next.f = c->input(curr.level, curr.index + 1);
      next.level = curr.level;
      next.index = curr.index + 1;
    }

    if (next.f) {
      smallest_key_priority_q.push(std::move(next));
    }
  }
  return true;
}

// Universal style of compaction. Pick files that are contiguous in
// time-range to compact.
//
std::unique_ptr<Compaction> UniversalCompactionPicker::PickCompaction(
    const std::string& cf_name,
    const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage,
    LogBuffer* log_buffer) {
  std::vector<std::vector<SortedRun>> sorted_runs = CalculateSortedRuns(
      *vstorage,
      ioptions_,
      mutable_cf_options.MaxFileSizeForCompaction());

  for (const auto& block : sorted_runs) {
    auto result = DoPickCompaction(cf_name, mutable_cf_options, vstorage, log_buffer, block);
    if (result != nullptr) {
      return result;
    }
  }
  TEST_SYNC_POINT("UniversalCompactionPicker::PickCompaction:SkippingCompaction");
  return nullptr;
}

std::unique_ptr<Compaction> UniversalCompactionPicker::DoPickCompaction(
    const std::string& cf_name,
    const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage,
    LogBuffer* log_buffer,
    const std::vector<UniversalCompactionPicker::SortedRun>& sorted_runs) {
  const int kLevel0 = 0;
  double score = vstorage->CompactionScore(kLevel0);
  std::unique_ptr<Compaction> c;

  if (sorted_runs.size() == 0) {
    RDEBUG(ioptions_.info_log, "[%s] Universal: nothing to do\n", cf_name.c_str());
    return nullptr;
  }
  VersionStorageInfo::LevelSummaryStorage tmp;
  RDEBUG(ioptions_.info_log,
         "[%s] Universal: sorted runs files(%" ROCKSDB_PRIszt "): %s\n",
         cf_name.c_str(), sorted_runs.size(),
         vstorage->LevelSummary(&tmp));

  // First, if we're using a compaction_file_filter_factory, check if we can directly delete this
  // set of files. This type of compaction may include any number of files, regardless of
  // level0_file_num_compaction_trigger.
  if (ioptions_.compaction_file_filter_factory) {
    c = PickCompactionUniversalDeletion(
        cf_name, mutable_cf_options, vstorage, score, sorted_runs, log_buffer);
  }
  if (c) {
    LOG_TO_BUFFER(log_buffer, "[%s] Universal: compacting for direct deletion\n",
                  cf_name.c_str());
  } else {
    // Check if the number of files to compact is greater than or equal to
    // level0_file_num_compaction_trigger. If so, consider size amplification and
    // read amplification as compaction reasons.
    if (sorted_runs.size() <
            (unsigned int)mutable_cf_options.level0_file_num_compaction_trigger) {
      RDEBUG(ioptions_.info_log, "[%s] Universal: nothing to do\n", cf_name.c_str());
      return nullptr;
    }

    // Check for size amplification next.
    c = PickCompactionUniversalSizeAmp(cf_name, mutable_cf_options, vstorage,
                                            score, sorted_runs, log_buffer);
    if (c) {
      LOG_TO_BUFFER(log_buffer, "[%s] Universal: compacting for size amp\n",
                  cf_name.c_str());
    } else {
      // Size amplification is within limits. Try reducing read
      // amplification while maintaining file size ratios.
      unsigned int ratio = ioptions_.compaction_options_universal.size_ratio;

      c = PickCompactionUniversalReadAmp(
          cf_name, mutable_cf_options, vstorage, score, ratio, UINT_MAX,
          ioptions_.compaction_options_universal.always_include_size_threshold,
          sorted_runs, log_buffer);
      if (c) {
        LOG_TO_BUFFER(log_buffer, "[%s] Universal: compacting for size ratio\n",
                    cf_name.c_str());
      } else {
        // ENG-1401: We trigger compaction logic when num files exceeds
        // level0_file_num_compaction_trigger. However, we only want to compact based on
        // files being of comparable sizes. This is already checked in the block above.
        // Previously, if we didn't find any such candidates, then we were falling down
        // into the block below to compact files without regards to their relative sizes,
        // if the number of files is greater than level0_file_num_compaction_trigger.
        // This was causing a lot of read/write amplification.
        //
        // Ideally, we should just remove this block below. For now, putting this
        // under a gflag.
        if (FLAGS_aggressive_compaction_for_read_amp) {
          // Size amplification and file size ratios are within configured limits.
          // If max read amplification is exceeding configured limits, then force
          // compaction without looking at filesize ratios and try to reduce
          // the number of files to fewer than level0_file_num_compaction_trigger.
          // This is guaranteed by NeedsCompaction()
          assert(sorted_runs.size() >=
                static_cast<size_t>(
                    mutable_cf_options.level0_file_num_compaction_trigger));
          unsigned int num_files =
          static_cast<unsigned int>(sorted_runs.size()) -
            mutable_cf_options.level0_file_num_compaction_trigger;
          if ((c = PickCompactionUniversalReadAmp(
                      cf_name, mutable_cf_options, vstorage, score, UINT_MAX, num_files,
                      ioptions_.compaction_options_universal.always_include_size_threshold,
                      sorted_runs, log_buffer)) != nullptr) {
            LOG_TO_BUFFER(log_buffer,
                          "[%s] Universal: compacting for file num -- %u\n",
                          cf_name.c_str(), num_files);
          }
        }
      }
    }
  }
  if (c == nullptr) {
    return nullptr;
  }

  if (ioptions_.compaction_options_universal.allow_trivial_move == true) {
    c->set_is_trivial_move(IsInputNonOverlapping(c.get()));
  }

// validate that all the chosen files of L0 are non overlapping in time
#ifndef NDEBUG
  SequenceNumber prev_smallest_seqno = 0U;
  bool is_first = true;

  size_t level_index = 0U;
  if (c->start_level() == 0) {
    for (auto f : *c->inputs(0)) {
      DCHECK_LE(f->smallest.seqno, f->largest.seqno);
      if (is_first) {
        is_first = false;
      } else {
        DCHECK_GT(prev_smallest_seqno, f->largest.seqno);
      }
      prev_smallest_seqno = f->smallest.seqno;
    }
    level_index = 1U;
  }
  for (; level_index < c->num_input_levels(); level_index++) {
    if (c->num_input_files(level_index) != 0) {
      SequenceNumber smallest_seqno = 0U;
      SequenceNumber largest_seqno = 0U;
      GetSmallestLargestSeqno(*(c->inputs(level_index)), &smallest_seqno,
                              &largest_seqno);
      if (is_first) {
        is_first = false;
      } else if (prev_smallest_seqno > 0) {
        // A level is considered as the bottommost level if there are
        // no files in higher levels or if files in higher levels do
        // not overlap with the files being compacted. Sequence numbers
        // of files in bottommost level can be set to 0 to help
        // compression. As a result, the following assert may not hold
        // if the prev_smallest_seqno is 0.
        assert(prev_smallest_seqno > largest_seqno);
      }
      prev_smallest_seqno = smallest_seqno;
    }
  }
#endif
  // update statistics
  MeasureTime(ioptions_.statistics, NUM_FILES_IN_SINGLE_COMPACTION,
              c->inputs(0)->size());

  level0_compactions_in_progress_.insert(c.get());

  return c;
}

uint32_t UniversalCompactionPicker::GetPathId(
    const ImmutableCFOptions& ioptions, uint64_t file_size) {
  // Two conditions need to be satisfied:
  // (1) the target path needs to be able to hold the file's size
  // (2) Total size left in this and previous paths need to be not
  //     smaller than expected future file size before this new file is
  //     compacted, which is estimated based on size_ratio.
  // For example, if now we are compacting files of size (1, 1, 2, 4, 8),
  // we will make sure the target file, probably with size of 16, will be
  // placed in a path so that eventually when new files are generated and
  // compacted to (1, 1, 2, 4, 8, 16), all those files can be stored in or
  // before the path we chose.
  //
  // TODO(sdong): now the case of multiple column families is not
  // considered in this algorithm. So the target size can be violated in
  // that case. We need to improve it.
  uint64_t accumulated_size = 0;
  uint64_t future_size = file_size *
    (100 - ioptions.compaction_options_universal.size_ratio) / 100;
  uint32_t p = 0;
  assert(!ioptions.db_paths.empty());
  for (; p < ioptions.db_paths.size() - 1; p++) {
    uint64_t target_size = ioptions.db_paths[p].target_size;
    if (target_size > file_size &&
        accumulated_size + (target_size - file_size) > future_size) {
      return p;
    }
    accumulated_size += target_size;
  }
  return p;
}

//
// Consider compaction files based on their size differences with
// the next file in time order.
//
std::unique_ptr<Compaction> UniversalCompactionPicker::PickCompactionUniversalReadAmp(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, double score, unsigned int ratio,
    unsigned int max_number_of_files_to_compact, size_t always_include_size_threshold,
    const std::vector<SortedRun>& sorted_runs, LogBuffer* log_buffer) {
  unsigned int min_merge_width =
    ioptions_.compaction_options_universal.min_merge_width;
  unsigned int max_merge_width =
    ioptions_.compaction_options_universal.max_merge_width;

  const SortedRun* sr = nullptr;
  bool done = false;
  size_t start_index = 0;
  unsigned int candidate_count = 0;

  unsigned int max_files_to_compact = std::min(max_merge_width,
                                       max_number_of_files_to_compact);
  min_merge_width = std::max(min_merge_width, 2U);

  // Caller checks the size before executing this function. This invariant is
  // important because otherwise we may have a possible integer underflow when
  // dealing with unsigned types.
  assert(sorted_runs.size() > 0);

  // Considers a candidate file only if it is smaller than the
  // total size accumulated so far.
  for (size_t loop = 0; loop < sorted_runs.size(); loop++) {
    candidate_count = 0;

    // Skip files that are already being compacted
    for (sr = nullptr; loop < sorted_runs.size(); loop++) {
      sr = &sorted_runs[loop];

      if (!sr->being_compacted) {
        candidate_count = 1;
        break;
      }
      char file_num_buf[kFormatFileNumberBufSize];
      sr->Dump(file_num_buf, sizeof(file_num_buf));
      RDEBUG(ioptions_.info_log, "[%s] Universal: %s[%d] being compacted, skipping",
              cf_name.c_str(), file_num_buf, loop);

      sr = nullptr;
    }

    // This file is not being compacted. Consider it as the
    // first candidate to be compacted.
    uint64_t candidate_size = sr != nullptr ? sr->compensated_file_size : 0;
    if (sr != nullptr) {
      char file_num_buf[kFormatFileNumberBufSize];
      sr->Dump(file_num_buf, sizeof(file_num_buf), true);
      RDEBUG(ioptions_.info_log, "[%s] Universal: Possible candidate %s[%d].",
             cf_name.c_str(), file_num_buf, loop);
    }

    // Check if the succeeding files need compaction.
    for (size_t i = loop + 1;
         candidate_count < max_files_to_compact && i < sorted_runs.size();
         i++) {
      const SortedRun* succeeding_sr = &sorted_runs[i];
      if (succeeding_sr->being_compacted) {
        break;
      }
      // Pick files if the total/last candidate file size (increased by the specified ratio) is
      // still larger than the next candidate file or if the next candidate file has size no more
      // than always_include_size_threshold.
      // candidate_size is the total size of files picked so far with the default
      // kCompactionStopStyleTotalSize;
      // with kCompactionStopStyleSimilarSize, it's simply the size of the last picked file.
      const bool is_include_by_threshold = succeeding_sr->size <= always_include_size_threshold;
      double sz = candidate_size * (100.0 + ratio) / 100.0;
      if (sz < static_cast<double>(succeeding_sr->size) && !is_include_by_threshold) {
        break;
      }
      if (ioptions_.compaction_options_universal.stop_style == kCompactionStopStyleSimilarSize) {
        if (!is_include_by_threshold) {
          // Similar-size stopping rule: also check the last picked file isn't
          // far larger than the next candidate file.
          sz = (succeeding_sr->size * (100.0 + ratio)) / 100.0;
          if (sz < static_cast<double>(candidate_size)) {
            // If the small file we've encountered begins a run of similar-size
            // files, we'll pick them up on a future iteration of the outer
            // loop. If it's some lonely straggler, it'll eventually get picked
            // by the last-resort read amp strategy which disregards size ratios.
            break;
          }
          candidate_size = succeeding_sr->compensated_file_size;
        }
      } else {  // default kCompactionStopStyleTotalSize
        candidate_size += succeeding_sr->compensated_file_size;
      }
      candidate_count++;
    }

    // Found a series of consecutive files that need compaction.
    if (candidate_count >= (unsigned int)min_merge_width) {
      start_index = loop;
      done = true;
      break;
    } else {
#ifndef NDEBUG
      for (size_t i = loop;
           i < loop + candidate_count && i < sorted_runs.size(); i++) {
        const SortedRun* skipping_sr = &sorted_runs[i];
        char file_num_buf[kFormatFileSizeInfoBufSize];
        skipping_sr->DumpSizeInfo(file_num_buf, sizeof(file_num_buf), loop);
        RDEBUG(ioptions_.info_log, "[%s] Universal: Skipping %s", cf_name.c_str(), file_num_buf);
      }
#endif
    }
  }
  if (!done || candidate_count <= 1) {
    return nullptr;
  }
  size_t first_index_after = start_index + candidate_count;
  // Compression is enabled if files compacted earlier already reached
  // size ratio of compression.
  bool enable_compression = true;
  int ratio_to_compress =
      ioptions_.compaction_options_universal.compression_size_percent;
  if (ratio_to_compress >= 0) {
    uint64_t total_size = 0;
    for (auto& sorted_run : sorted_runs) {
      total_size += sorted_run.compensated_file_size;
    }

    uint64_t older_file_size = 0;
    for (size_t i = sorted_runs.size() - 1; i >= first_index_after; i--) {
      older_file_size += sorted_runs[i].size;
      if (older_file_size * 100L >= total_size * static_cast<int64_t>(ratio_to_compress)) {
        enable_compression = false;
        break;
      }
    }
  }

  uint64_t estimated_total_size = 0;
  for (unsigned int i = 0; i < first_index_after; i++) {
    estimated_total_size += sorted_runs[i].size;
  }
  uint32_t path_id = GetPathId(ioptions_, estimated_total_size);
  int start_level = sorted_runs[start_index].level;
  int output_level;
  if (first_index_after == sorted_runs.size()) {
    output_level = vstorage->num_levels() - 1;
  } else if (sorted_runs[first_index_after].level == 0) {
    output_level = 0;
  } else {
    output_level = sorted_runs[first_index_after].level - 1;
  }

  std::vector<CompactionInputFiles> inputs(vstorage->num_levels());
  for (size_t i = 0; i < inputs.size(); ++i) {
    inputs[i].level = start_level + static_cast<int>(i);
  }
  for (size_t i = start_index; i < first_index_after; i++) {
    auto& picking_sr = sorted_runs[i];
    if (picking_sr.level == 0) {
      FileMetaData* picking_file = picking_sr.file;
      inputs[0].files.push_back(picking_file);
    } else {
      auto& files = inputs[picking_sr.level - start_level].files;
      for (auto* f : vstorage->LevelFiles(picking_sr.level)) {
        files.push_back(f);
      }
    }
    char file_num_buf[kFormatFileSizeInfoBufSize];
    picking_sr.DumpSizeInfo(file_num_buf, sizeof(file_num_buf), i);
    LOG_TO_BUFFER(log_buffer, "[%s] Universal: Picking %s", cf_name.c_str(),
                file_num_buf);
  }

  CompactionReason compaction_reason;
  if (max_number_of_files_to_compact == UINT_MAX) {
    compaction_reason = CompactionReason::kUniversalSortedRunNum;
  } else {
    compaction_reason = CompactionReason::kUniversalSizeRatio;
  }
  return Compaction::Create(
      vstorage, mutable_cf_options, std::move(inputs), output_level,
      mutable_cf_options.MaxFileSizeForLevel(output_level), LLONG_MAX, path_id,
      GetCompressionType(ioptions_, start_level, 1, enable_compression),
      /* grandparents = */ std::vector<FileMetaData*>(), ioptions_.info_log,
      /* is_manual = */ false, score,
      /* deletion_compaction = */ false, compaction_reason);
}

// Look to see if all files within the run are marked for deletion.
// If so, we can run a low-cost compaction that just deletes those files.
std::unique_ptr<Compaction> UniversalCompactionPicker::PickCompactionUniversalDeletion(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, double score,
    const std::vector<SortedRun>& sorted_runs, LogBuffer* log_buffer) {

  // Universal deletion compaction is currently only compatible with Level-0
  // universal compactions.
  if (vstorage->num_levels() > 1) {
    RLOG(InfoLogLevel::WARN_LEVEL, ioptions_.info_log,
        "[%s] Unexpected number of storage levels during universal deletion "
        "(expected 1, was %d).",
        cf_name.c_str(), vstorage->num_levels());
    return nullptr;
  }
  std::vector<CompactionInputFiles> inputs(1);
  auto& input_files = inputs[0];

  // Check each file to see if they are marked for deletion.
  for (size_t loop = 0; loop < sorted_runs.size(); loop++) {
    const auto sr = &sorted_runs[loop];

    if (!sr->being_compacted && sr->delete_after_compaction()) {
      FileMetaData* f = sr->file;
      input_files.files.push_back(f);

      char file_num_buf[kFormatFileSizeInfoBufSize];
      sr->DumpSizeInfo(file_num_buf, sizeof(file_num_buf), loop);
      LOG_TO_BUFFER(log_buffer, "[%s] Universal: file deletion picking %s",
                  cf_name.c_str(), file_num_buf);
    } else {
      RDEBUG(ioptions_.info_log, "[%s] Universal: skipping %s[%d] compacted %s\n",
          cf_name.c_str(), sr->FileNumAsString().c_str(), loop,
          sr->delete_after_compaction() ?
              "is not marked for deletion." : "is already being compacted.");
    }
  }
  // If no files are marked for deletion, return nullptr.
  if (input_files.size() == 0) {
    return nullptr; // no candidate files
  }

  return Compaction::Create(
      vstorage,
      mutable_cf_options,
      std::move(inputs),
      /* output level = */ 0,
      mutable_cf_options.MaxFileSizeForLevel(0),
      /* max_grandparent_overlap_bytes = */ LLONG_MAX,
      /* path_id = */ 0,
      GetCompressionType(ioptions_, 0, 1),
      /* grandparents = */ {},
      ioptions_.info_log,
      /* is manual = */ false,
      score,
      /* deletion_compaction not currently used, but could be in the future */ false,
      CompactionReason::kUniversalDirectDeletion);
}

// Look at overall size amplification. If size amplification
// exceeeds the configured value, then do a compaction
// of the candidate files all the way upto the earliest
// base file (overrides configured values of file-size ratios,
// min_merge_width and max_merge_width).
//
std::unique_ptr<Compaction> UniversalCompactionPicker::PickCompactionUniversalSizeAmp(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, double score,
    const std::vector<SortedRun>& sorted_runs, LogBuffer* log_buffer) {
  // percentage flexibilty while reducing size amplification
  uint64_t ratio = ioptions_.compaction_options_universal.
                     max_size_amplification_percent;

  unsigned int candidate_count = 0;
  uint64_t candidate_size = 0;
  size_t start_index = 0;
  const SortedRun* sr = nullptr;

  // Skip files that are already being compacted
  for (size_t loop = 0; loop < sorted_runs.size() - 1; loop++) {
    sr = &sorted_runs[loop];
    if (!sr->being_compacted) {
      start_index = loop;         // Consider this as the first candidate.
      break;
    }
    RDEBUG(ioptions_.info_log, "[%s] Universal: Possible candidate %s[%d] is already being "
        "compacted. Cannot be a candidate to reduce size amp.\n",
        cf_name.c_str(), sr->FileNumAsString().c_str(), loop);
    sr = nullptr;
  }

  if (sr == nullptr) {
    return nullptr;             // no candidate files
  }
  {
    char file_num_buf[kFormatFileNumberBufSize];
    sr->Dump(file_num_buf, sizeof(file_num_buf), true);
    RDEBUG(ioptions_.info_log,
           "[%s] Universal: First candidate %s[%" ROCKSDB_PRIszt "] %s",
           cf_name.c_str(), file_num_buf, start_index,
           " to reduce size amp.\n");
  }

  // keep adding up all the remaining files
  for (size_t loop = start_index; loop < sorted_runs.size() - 1; loop++) {
    sr = &sorted_runs[loop];
    if (sr->being_compacted) {
      RDEBUG(ioptions_.info_log, "[%s] Universal: Possible candidate %s[%d] is already being "
          "compacted. No size amp reduction possible.\n",
          cf_name.c_str(), sr->FileNumAsString().c_str(), loop);
      return nullptr;
    }
    candidate_size += sr->compensated_file_size;
    candidate_count++;
  }
  if (candidate_count == 0) {
    return nullptr;
  }

  // Check that the earliest file is not already being compacted.
  sr = &sorted_runs.back();
  if (sr->being_compacted) {
    RDEBUG(ioptions_.info_log, "[%s] Universal: Possible candidate %s[%d] is already being "
          "compacted. No size amp reduction possible.\n",
          cf_name.c_str(), sr->FileNumAsString().c_str(), sorted_runs.size() - 1);
    return nullptr;
  }
  // size of earliest file
  uint64_t earliest_file_size = sr->size;

  // size amplification = percentage of additional size
  if (candidate_size * 100 < ratio * earliest_file_size) {
    RDEBUG(ioptions_.info_log,
           "[%s] Universal: size amp not needed. newer-files-total-size %" PRIu64
           " earliest-file-size %" PRIu64,
           cf_name.c_str(), candidate_size, earliest_file_size);
    return nullptr;
  } else {
    RDEBUG(ioptions_.info_log,
           "[%s] Universal: size amp needed. newer-files-total-size %" PRIu64
           " earliest-file-size %" PRIu64,
           cf_name.c_str(), candidate_size, earliest_file_size);
  }
  assert(start_index < sorted_runs.size() - 1);

  // Estimate total file size
  uint64_t estimated_total_size = 0;
  for (size_t loop = start_index; loop < sorted_runs.size(); loop++) {
    estimated_total_size += sorted_runs[loop].size;
  }
  uint32_t path_id = GetPathId(ioptions_, estimated_total_size);
  int start_level = sorted_runs[start_index].level;

  std::vector<CompactionInputFiles> inputs(vstorage->num_levels());
  for (size_t i = 0; i < inputs.size(); ++i) {
    inputs[i].level = start_level + static_cast<int>(i);
  }
  // We always compact all the files, so always compress.
  for (size_t loop = start_index; loop < sorted_runs.size(); loop++) {
    auto& picking_sr = sorted_runs[loop];
    if (picking_sr.level == 0) {
      FileMetaData* f = picking_sr.file;
      inputs[0].files.push_back(f);
    } else {
      auto& files = inputs[picking_sr.level - start_level].files;
      for (auto* f : vstorage->LevelFiles(picking_sr.level)) {
        files.push_back(f);
      }
    }
    char file_num_buf[kFormatFileSizeInfoBufSize];
    picking_sr.DumpSizeInfo(file_num_buf, sizeof(file_num_buf), loop);
    LOG_TO_BUFFER(log_buffer, "[%s] Universal: size amp picking %s",
                cf_name.c_str(), file_num_buf);
  }

  return Compaction::Create(
      vstorage, mutable_cf_options, std::move(inputs), vstorage->num_levels() - 1,
      mutable_cf_options.MaxFileSizeForLevel(vstorage->num_levels() - 1),
      /* max_grandparent_overlap_bytes */ LLONG_MAX, path_id,
      GetCompressionType(ioptions_, vstorage->num_levels() - 1, 1),
      /* grandparents */ std::vector<FileMetaData*>(), ioptions_.info_log, /* is manual = */ false,
      score, false /* deletion_compaction */, CompactionReason::kUniversalSizeAmplification);
}

bool FIFOCompactionPicker::NeedsCompaction(const VersionStorageInfo* vstorage)
    const {
  const int kLevel0 = 0;
  return vstorage->CompactionScore(kLevel0) >= 1;
}

std::unique_ptr<Compaction> FIFOCompactionPicker::PickCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, LogBuffer* log_buffer) {
  assert(vstorage->num_levels() == 1);
  const int kLevel0 = 0;
  const std::vector<FileMetaData*>& level_files = vstorage->LevelFiles(kLevel0);
  uint64_t total_size = 0;
  for (const auto& file : level_files) {
    total_size += file->fd.total_file_size;
  }

  if (total_size <= ioptions_.compaction_options_fifo.max_table_files_size ||
      level_files.size() == 0) {
    // total size not exceeded
    RDEBUG(ioptions_.info_log,
           "[%s] FIFO compaction: nothing to do. Total size %" PRIu64
           ", max size %" PRIu64 "\n",
           cf_name.c_str(), total_size,
           ioptions_.compaction_options_fifo.max_table_files_size);
    return nullptr;
  }

  if (!level0_compactions_in_progress_.empty()) {
    RDEBUG(ioptions_.info_log,
           "[%s] FIFO compaction: Already executing compaction. No need "
           "to run parallel compactions since compactions are very fast",
           cf_name.c_str());
    return nullptr;
  }

  std::vector<CompactionInputFiles> inputs;
  inputs.emplace_back();
  inputs[0].level = 0;
  // delete old files (FIFO)
  for (auto ritr = level_files.rbegin(); ritr != level_files.rend(); ++ritr) {
    auto f = *ritr;
    total_size -= f->compensated_file_size;
    inputs[0].files.push_back(f);
    char tmp_fsize[16];
    AppendHumanBytes(f->fd.GetTotalFileSize(), tmp_fsize, sizeof(tmp_fsize));
    LOG_TO_BUFFER(log_buffer, "[%s] FIFO compaction: picking file %" PRIu64
                            " with size %s for deletion",
                cf_name.c_str(), f->fd.GetNumber(), tmp_fsize);
    if (total_size <= ioptions_.compaction_options_fifo.max_table_files_size) {
      break;
    }
  }
  auto c = Compaction::Create(
      vstorage, mutable_cf_options, std::move(inputs), 0 /* output_level */,
      0 /* target_file_size */, 0 /* max_grandparent_overlap_bytes */, 0 /* output_path_id */,
      kNoCompression, std::vector<FileMetaData*>(), ioptions_.info_log, /* is manual */ false,
      vstorage->CompactionScore(0),
      /* is deletion compaction */ true, CompactionReason::kFIFOMaxSize);
  if (c) {
    level0_compactions_in_progress_.insert(c.get());
  }
  return c;
}

std::unique_ptr<Compaction> FIFOCompactionPicker::CompactRange(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, int input_level, int output_level,
    uint32_t output_path_id, const InternalKey* begin, const InternalKey* end,
    CompactionReason compaction_reason, InternalKey** compaction_end,
    bool* manual_conflict) {
  assert(input_level == 0);
  assert(output_level == 0);
  *compaction_end = nullptr;
  LogBuffer log_buffer(InfoLogLevel::INFO_LEVEL, ioptions_.info_log);
  auto c = PickCompaction(cf_name, mutable_cf_options, vstorage, &log_buffer);
  log_buffer.FlushBufferToLog();
  return c;
}


}  // namespace rocksdb
