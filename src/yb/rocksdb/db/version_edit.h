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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/gutil/atomicops.h"

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/listener.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"

namespace rocksdb {

class TableCache;
class VersionSet;
class VersionEditPB;

const uint64_t kFileNumberMask = 0x3FFFFFFFFFFFFFFF;

extern uint64_t PackFileNumberAndPathId(uint64_t number, uint64_t path_id);

// A copyable structure contains information needed to read data from an SST
// file. It can contains a pointer to a table reader opened for the file, or
// file number and size, which can be used to create a new table reader for it.
// The behavior is undefined when a copied of the structure is used when the
// file is not in any live version any more.
// SST can be either one file containing both meta data and data or it can be split into
// multiple files: one metadata file and number of data files (S-Blocks aka storage-blocks).
// As of 2017-03-10 there is at most one data file.
// Base file is a file which contains SST metadata. So, if SST is either one base file, or
// in case SST is split into multiple files, base file is a metadata file.
struct FileDescriptor {
  // Table reader in table_reader_handle
  TableReader* table_reader;
  uint64_t packed_number_and_path_id;
  uint64_t total_file_size;  // total file(s) size in bytes
  uint64_t base_file_size;  // base file size in bytes

  FileDescriptor() : FileDescriptor(0, 0, 0, 0) {}

  FileDescriptor(uint64_t number, uint32_t path_id, uint64_t _total_file_size,
      uint64_t _base_file_size)
      : table_reader(nullptr),
        packed_number_and_path_id(PackFileNumberAndPathId(number, path_id)),
        total_file_size(_total_file_size),
        base_file_size(_base_file_size) {}

  uint64_t GetNumber() const {
    return packed_number_and_path_id & kFileNumberMask;
  }
  uint32_t GetPathId() const {
    return static_cast<uint32_t>(
        packed_number_and_path_id / (kFileNumberMask + 1));
  }
  uint64_t GetTotalFileSize() const { return total_file_size; }
  uint64_t GetBaseFileSize() const { return base_file_size; }

  std::string ToString() const;
};

YB_DEFINE_ENUM(UpdateBoundariesType, (kAll)(kSmallest)(kLargest));

struct FileMetaData {
  typedef FileBoundaryValues<InternalKey> BoundaryValues;

  int refs;
  FileDescriptor fd;
  bool being_compacted;        // Is this file undergoing compaction?
  bool being_deleted = false;  // Updated by DB::DeleteFile
  BoundaryValues smallest;     // The smallest values in this file
  BoundaryValues largest;      // The largest values in this file
  bool imported = false;       // Was this file imported from another DB.

  // Needs to be disposed when refs becomes 0.
  Cache::Handle* table_reader_handle;

  // Stats for compensating deletion entries during compaction

  // File size compensated by deletion entry.
  // This is updated in Version::UpdateAccumulatedStats() first time when the
  // file is created or loaded.  After it is updated (!= 0), it is immutable.
  uint64_t compensated_file_size;
  // These values can mutate, but they can only be read or written from
  // single-threaded LogAndApply thread
  uint64_t num_entries;            // the number of entries.
  uint64_t num_deletions;          // the number of deletion entries.
  uint64_t raw_key_size;           // total uncompressed key size.
  uint64_t raw_value_size;         // total uncompressed value size.
  bool init_stats_from_file;   // true if the data-entry stats of this file
                               // has initialized from file.

  bool marked_for_compaction;  // True if client asked us nicely to compact this
                               // file.

  bool delete_after_compaction() const {
    return base::subtle::NoBarrier_Load(&delete_after_compaction_) != 0;
  }

  void set_delete_after_compaction(bool value) {
    base::subtle::Release_Store(&delete_after_compaction_, value ? 1 : 0);
  }

  FileMetaData();

  void UpdateKey(const Slice& key, UpdateBoundariesType type);

  // Update all boundaries except key.
  void UpdateBoundariesExceptKey(const BoundaryValues& source, UpdateBoundariesType type);

  void UpdateBoundarySeqNo(SequenceNumber sequence_number);

  void UpdateBoundaryUserValues(const UserBoundaryValueRefs& source, UpdateBoundariesType type);

  bool Unref(TableCache* table_cache);

  Slice UserFilter() const; // Extracts user filter from largest boundary value if present.

  // Outputs smallest and largest user frontiers to string, if they exist.
  std::string FrontiersToString() const;

  std::string ToString() const;

 private:
  // True if file has been marked for direct deletion.
  // We cannot use std::atomic<bool> here, because this class is stored in std::vector.
  AtomicWord delete_after_compaction_ = 0;
};

class VersionEdit {
 public:
  VersionEdit() { Clear(); }
  ~VersionEdit() { }

  void Clear();

  void SetComparatorName(const Slice& name) {
    comparator_ = name.ToString();
  }
  void SetLogNumber(uint64_t num) {
    log_number_ = num;
  }
  void SetPrevLogNumber(uint64_t num) {
    prev_log_number_ = num;
  }
  void SetNextFile(uint64_t num) {
    next_file_number_ = num;
  }
  void SetLastSequence(SequenceNumber seq) {
    last_sequence_ = seq;
  }
  void UpdateFlushedFrontier(UserFrontierPtr value);
  void ModifyFlushedFrontier(UserFrontierPtr value, FrontierModificationMode mode);
  void SetMaxColumnFamily(uint32_t max_column_family) {
    max_column_family_ = max_column_family;
  }

  void InitNewDB();

  // Add the specified file at the specified number.
  // REQUIRES: This version has not been saved (see VersionSet::SaveTo)
  // REQUIRES: "smallest" and "largest" are smallest and largest keys in file
  void AddTestFile(int level,
                   const FileDescriptor& fd,
                   const FileMetaData::BoundaryValues& smallest,
                   const FileMetaData::BoundaryValues& largest,
                   bool marked_for_compaction);

  void AddFile(int level, const FileMetaData& f);

  void AddCleanedFile(int level, const FileMetaData& f);

  // Delete the specified "file" from the specified "level".
  void DeleteFile(int level, uint64_t file) {
    deleted_files_.insert({level, file});
  }

  // Number of edits
  size_t NumEntries() { return new_files_.size() + deleted_files_.size(); }

  bool IsColumnFamilyAdd() {
    return column_family_name_ ? true : false;
  }

  bool IsColumnFamilyManipulation() {
    return IsColumnFamilyAdd() || is_column_family_drop_;
  }

  void SetColumnFamily(uint32_t column_family_id) {
    column_family_ = column_family_id;
  }

  // set column family ID by calling SetColumnFamily()
  void AddColumnFamily(const std::string& name) {
    DCHECK(!is_column_family_drop_);
    DCHECK(!column_family_name_);
    DCHECK_EQ(NumEntries(), 0);
    column_family_name_ = name;
  }

  // set column family ID by calling SetColumnFamily()
  void DropColumnFamily() {
    DCHECK(!is_column_family_drop_);
    DCHECK(!column_family_name_);
    DCHECK_EQ(NumEntries(), 0);
    is_column_family_drop_ = true;
  }

  // return true on success.
  bool AppendEncodedTo(std::string* dst) const;
  Status DecodeFrom(BoundaryValuesExtractor* extractor, const Slice& src);

  typedef std::set<std::pair<int, uint64_t>> DeletedFileSet;

  const DeletedFileSet& GetDeletedFiles() { return deleted_files_; }
  const std::vector<std::pair<int, FileMetaData>>& GetNewFiles() {
    return new_files_;
  }

  std::string DebugString(bool hex_key = false) const;

  std::string ToString() const {
    return DebugString();
  }

 private:
  friend class VersionSet;
  friend class Version;

  bool EncodeTo(VersionEditPB* out) const;

  int max_level_;
  std::optional<std::string> comparator_;
  std::optional<uint64_t> log_number_;
  std::optional<uint64_t> prev_log_number_;
  std::optional<uint64_t> next_file_number_;
  std::optional<uint32_t> max_column_family_;
  std::optional<SequenceNumber> last_sequence_;
  UserFrontierPtr flushed_frontier_;

  // Used when we're resetting the flushed frontier to a potentially lower value. This is needed
  // when restoring from a backup into a new Raft group with an unrelated sequence of OpIds.
  bool force_flushed_frontier_ = false;

  DeletedFileSet deleted_files_;
  std::vector<std::pair<int, FileMetaData>> new_files_;

  // Each version edit record should have column_family_id set
  // If it's not set, it is default (0)
  uint32_t column_family_;
  // a version edit can be either column_family add or
  // column_family drop. If it's column family add,
  // it also includes column family name.
  bool is_column_family_drop_;
  std::optional<std::string> column_family_name_;
};

}  // namespace rocksdb
