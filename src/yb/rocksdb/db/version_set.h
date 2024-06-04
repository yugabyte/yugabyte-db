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
//
// The representation of a DBImpl consists of a set of Versions.  The
// newest version is called "current".  Older versions may be kept
// around to provide a consistent view to live iterators.
//
// Each Version keeps track of a set of Table files per level.  The
// entire set of versions is maintained in a VersionSet.
//
// Version,VersionSet are thread-compatible, but require external
// synchronization on all accesses.


#pragma once

#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/version_builder.h"
#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/db/table_cache.h"
#include "yb/rocksdb/db/compaction_picker.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/log_reader.h"
#include "yb/rocksdb/db/file_indexer.h"
#include "yb/rocksdb/db/write_controller.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/instrumented_mutex.h"

namespace rocksdb {

namespace log {
class Writer;
}

class ColumnFamilyData;
class ColumnFamilySet;
class Compaction;
class FileNumbersProvider;
class InternalIterator;
class LogBuffer;
class LookupKey;
class MemTable;
class MergeContext;
class TableCache;
class Version;
class VersionSet;
class WriteBuffer;

// Return the smallest index i such that file_level.files[i]->largest >= key.
// Return file_level.num_files if there is no such file.
// REQUIRES: "file_level.files" contains a sorted list of
// non-overlapping files.
extern int FindFile(const InternalKeyComparator& icmp,
                    const LevelFilesBrief& file_level, const Slice& key);

// Returns true iff some file in "files" overlaps the user key range
// [*smallest,*largest].
// smallest==nullptr represents a key smaller than all keys in the DB.
// largest==nullptr represents a key largest than all keys in the DB.
// REQUIRES: If disjoint_sorted_files, file_level.files[]
// contains disjoint ranges in sorted order.
extern bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                                  bool disjoint_sorted_files,
                                  const LevelFilesBrief& file_level,
                                  const Slice* smallest_user_key,
                                  const Slice* largest_user_key);

// Generate LevelFilesBrief from vector<FdWithKeyRange*>
// Would copy smallest_key and largest_key data to sequential memory
// arena: Arena used to allocate the memory
extern void DoGenerateLevelFilesBrief(LevelFilesBrief* file_level,
                                      const std::vector<FileMetaData*>& files,
                                      Arena* arena);

class VersionStorageInfo {
 public:
  VersionStorageInfo(const InternalKeyComparatorPtr& internal_comparator,
                     const Comparator* user_comparator, int num_levels,
                     CompactionStyle compaction_style,
                     VersionStorageInfo* src_vstorage);
  ~VersionStorageInfo();

  void Reserve(int level, size_t size) { files_[level].reserve(size); }

  void AddFile(int level, FileMetaData* f, Logger* info_log = nullptr);

  void SetFinalized();

  // Update num_non_empty_levels_.
  void UpdateNumNonEmptyLevels();

  void GenerateFileIndexer() {
    file_indexer_.UpdateIndex(&arena_, num_non_empty_levels_, files_);
  }

  // Update the accumulated stats from a file-meta.
  void UpdateAccumulatedStats(FileMetaData* file_meta);

  // Decrease the current stat form a to-be-delected file-meta
  void RemoveCurrentStats(FileMetaData* file_meta);

  void ComputeCompensatedSizes();

  // Updates internal structures that keep track of compaction scores
  // We use compaction scores to figure out which compaction to do next
  // REQUIRES: db_mutex held!!
  // TODO find a better way to pass compaction_options_fifo.
  void ComputeCompactionScore(
      const MutableCFOptions& mutable_cf_options,
      const CompactionOptionsFIFO& compaction_options_fifo);

  // Estimate est_comp_needed_bytes_
  void EstimateCompactionBytesNeeded(
      const MutableCFOptions& mutable_cf_options);

  // This computes files_marked_for_compaction_ and is called by
  // ComputeCompactionScore()
  void ComputeFilesMarkedForCompaction();

  // Generate level_files_brief_ from files_
  void GenerateLevelFilesBrief();
  // Sort all files for this version based on their file size and
  // record results in files_by_compaction_pri_. The largest files are listed
  // first.
  void UpdateFilesByCompactionPri(const MutableCFOptions& mutable_cf_options);

  void GenerateLevel0NonOverlapping();
  bool level0_non_overlapping() const {
    return level0_non_overlapping_;
  }

  int MaxInputLevel() const;

  // Return level number that has idx'th highest score
  int CompactionScoreLevel(int idx) const { return compaction_level_[idx]; }

  // Return idx'th highest score
  double CompactionScore(int idx) const { return compaction_score_[idx]; }

  void GetOverlappingInputs(
      int level,
      const InternalKey* begin,   // nullptr means before all keys
      const InternalKey* end,     // nullptr means after all keys
      std::vector<FileMetaData*>* inputs,
      int hint_index = -1,        // index of overlap file
      int* file_index = nullptr,  // return index of overlap file
      bool expand_range = true)   // if set, returns files which overlap the
      const;                      // range and overlap each other. If false,
                                  // then just files intersecting the range

  void GetOverlappingInputsBinarySearch(
      int level,
      const Slice& begin,  // nullptr means before all keys
      const Slice& end,    // nullptr means after all keys
      std::vector<FileMetaData*>* inputs,
      int hint_index,          // index of overlap file
      int* file_index) const;  // return index of overlap file

  void ExtendOverlappingInputs(
      int level,
      const Slice& begin,  // nullptr means before all keys
      const Slice& end,    // nullptr means after all keys
      std::vector<FileMetaData*>* inputs,
      unsigned int index) const;  // start extending from this index

  // Returns true iff some file in the specified level overlaps
  // some part of [*smallest_user_key,*largest_user_key].
  // smallest_user_key==NULL represents a key smaller than all keys in the DB.
  // largest_user_key==NULL represents a key largest than all keys in the DB.
  bool OverlapInLevel(int level, const Slice* smallest_user_key,
                      const Slice* largest_user_key);

  // Returns true iff the first or last file in inputs contains
  // an overlapping user key to the file "just outside" of it (i.e.
  // just after the last file, or just before the first file)
  // REQUIRES: "*inputs" is a sorted list of non-overlapping files
  bool HasOverlappingUserKey(const std::vector<FileMetaData*>* inputs,
                             int level);

  int num_levels() const { return num_levels_; }

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  int num_non_empty_levels() const {
    assert(finalized_);
    return num_non_empty_levels_;
  }

  // REQUIRES: This version has been finalized.
  // (CalculateBaseBytes() is called)
  // This may or may not return number of level files. It is to keep backward
  // compatible behavior in universal compaction.
  int l0_delay_trigger_count() const { return l0_delay_trigger_count_; }

  void set_l0_delay_trigger_count(int v) { l0_delay_trigger_count_ = v; }

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  int NumLevelFiles(int level) const {
    assert(finalized_);
    return static_cast<int>(files_[level].size());
  }

  uint64_t NumFiles() const;

  // Return the combined file size of all files at the specified level.
  uint64_t NumLevelBytes(int level) const;

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  const std::vector<FileMetaData*>& LevelFiles(int level) const {
    return files_[level];
  }

  const rocksdb::LevelFilesBrief& LevelFilesBrief(int level) const {
    assert(level < static_cast<int>(level_files_brief_.size()));
    return level_files_brief_[level];
  }

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  const std::vector<int>& FilesByCompactionPri(int level) const {
    assert(finalized_);
    return files_by_compaction_pri_[level];
  }

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  // REQUIRES: DB mutex held during access
  const autovector<std::pair<int, FileMetaData*>>& FilesMarkedForCompaction()
      const {
    assert(finalized_);
    return files_marked_for_compaction_;
  }

  int base_level() const { return base_level_; }

  // REQUIRES: lock is held
  // Set the index that is used to offset into files_by_compaction_pri_ to find
  // the next compaction candidate file.
  void SetNextCompactionIndex(int level, int index) {
    next_file_to_compact_by_size_[level] = index;
  }

  // REQUIRES: lock is held
  int NextCompactionIndex(int level) const {
    return next_file_to_compact_by_size_[level];
  }

  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  const FileIndexer& file_indexer() const {
    assert(finalized_);
    return file_indexer_;
  }

  // Only the first few entries of files_by_compaction_pri_ are sorted.
  // There is no need to sort all the files because it is likely
  // that on a running system, we need to look at only the first
  // few largest files because a new version is created every few
  // seconds/minutes (because of concurrent compactions).
  static const size_t kNumberFilesToSort = 50;

  // Return a human-readable short (single-line) summary of the number
  // of files per level.  Uses *scratch as backing store.
  struct LevelSummaryStorage {
    char buffer[1000];
  };
  struct FileSummaryStorage {
    char buffer[3000];
  };
  const char* LevelSummary(LevelSummaryStorage* scratch) const;
  // Return a human-readable short (single-line) summary of files
  // in a specified level.  Uses *scratch as backing store.
  const char* LevelFileSummary(FileSummaryStorage* scratch, int level) const;

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  int64_t MaxNextLevelOverlappingBytes();

  uint64_t GetAverageValueSize() const {
    if (accumulated_num_non_deletions_ == 0) {
      return 0;
    }
    assert(accumulated_raw_key_size_ + accumulated_raw_value_size_ > 0);
    assert(accumulated_file_size_ > 0);
    return accumulated_raw_value_size_ / accumulated_num_non_deletions_ *
           accumulated_file_size_ /
           (accumulated_raw_key_size_ + accumulated_raw_value_size_);
  }

  uint64_t GetEstimatedActiveKeys() const;

  // re-initializes the index that is used to offset into
  // files_by_compaction_pri_
  // to find the next compaction candidate file.
  void ResetNextCompactionIndex(int level) {
    next_file_to_compact_by_size_[level] = 0;
  }

  const InternalKeyComparatorPtr& InternalComparator() {
    return internal_comparator_;
  }

  // Returns maximum total bytes of data on a given level.
  uint64_t MaxBytesForLevel(int level) const;

  // Must be called after any change to MutableCFOptions.
  void CalculateBaseBytes(const ImmutableCFOptions& ioptions,
                          const MutableCFOptions& options);

  // Returns an estimate of the amount of live data in bytes.
  uint64_t EstimateLiveDataSize() const;

  uint64_t estimated_compaction_needed_bytes() const {
    return estimated_compaction_needed_bytes_;
  }

  void TEST_set_estimated_compaction_needed_bytes(uint64_t v) {
    estimated_compaction_needed_bytes_ = v;
  }

 private:
  InternalKeyComparatorPtr internal_comparator_;
  const Comparator* user_comparator_;
  int num_levels_;            // Number of levels
  int num_non_empty_levels_;  // Number of levels. Any level larger than it
                              // is guaranteed to be empty.
  // Per-level max bytes
  std::vector<uint64_t> level_max_bytes_;

  // A short brief metadata of files per level
  autovector<rocksdb::LevelFilesBrief> level_files_brief_;
  FileIndexer file_indexer_;
  Arena arena_;  // Used to allocate space for file_levels_

  CompactionStyle compaction_style_;

  // List of files per level, files in each level are arranged
  // in increasing order of keys
  std::vector<FileMetaData*>* files_;

  // Level that L0 data should be compacted to. All levels < base_level_ should
  // be empty. -1 if it is not level-compaction so it's not applicable.
  int base_level_;

  // A list for the same set of files that are stored in files_,
  // but files in each level are now sorted based on file
  // size. The file with the largest size is at the front.
  // This vector stores the index of the file from files_.
  std::vector<std::vector<int>> files_by_compaction_pri_;

  // If true, means that files in L0 have keys with non overlapping ranges
  bool level0_non_overlapping_;

  // An index into files_by_compaction_pri_ that specifies the first
  // file that is not yet compacted
  std::vector<int> next_file_to_compact_by_size_;

  // Only the first few entries of files_by_compaction_pri_ are sorted.
  // There is no need to sort all the files because it is likely
  // that on a running system, we need to look at only the first
  // few largest files because a new version is created every few
  // seconds/minutes (because of concurrent compactions).
  static const size_t number_of_files_to_sort_ = 50;

  // This vector contains list of files marked for compaction and also not
  // currently being compacted. It is protected by DB mutex. It is calculated in
  // ComputeCompactionScore()
  autovector<std::pair<int, FileMetaData*>> files_marked_for_compaction_;

  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  // The most critical level to be compacted is listed first
  // These are used to pick the best compaction level
  std::vector<double> compaction_score_;
  std::vector<int> compaction_level_;
  int l0_delay_trigger_count_ = 0;  // Count used to trigger slow down and stop
                                    // for number of L0 files.

  // the following are the sampled temporary stats.
  // the current accumulated size of sampled files.
  uint64_t accumulated_file_size_;
  // the current accumulated size of all raw keys based on the sampled files.
  uint64_t accumulated_raw_key_size_;
  // the current accumulated size of all raw keys based on the sampled files.
  uint64_t accumulated_raw_value_size_;
  // total number of non-deletion entries
  uint64_t accumulated_num_non_deletions_;
  // total number of deletion entries
  uint64_t accumulated_num_deletions_;
  // current number of non_deletion entries
  uint64_t current_num_non_deletions_;
  // current number of delection entries
  uint64_t current_num_deletions_;
  // current number of file samples
  uint64_t current_num_samples_;
  // Estimated bytes needed to be compacted until all levels' size is down to
  // target sizes.
  uint64_t estimated_compaction_needed_bytes_;

  bool finalized_;

  friend class Version;
  friend class VersionSet;
  // No copying allowed
  VersionStorageInfo(const VersionStorageInfo&) = delete;
  void operator=(const VersionStorageInfo&) = delete;
};

class Version {
 public:
  // Append to *iters a sequence of iterators that will
  // yield the contents of this Version when merged together.
  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  void AddIterators(const ReadOptions&, const EnvOptions& soptions,
                    MergeIteratorBuilder* merger_iter_builder);
  template<typename MergeIteratorBuilderType>
  void AddIndexIterators(
      const ReadOptions& read_options, const EnvOptions& soptions,
      MergeIteratorBuilderType* merge_iter_builder);

  // Lookup the value for key.  If found, store it in *val and
  // return OK.  Else return a non-OK status.
  // Uses *operands to store merge_operator operations to apply later.
  //
  // If the ReadOptions.read_tier is set to do a read-only fetch, then
  // *value_found will be set to false if it cannot be determined whether
  // this value exists without doing IO.
  //
  // If the key is Deleted, *status will be set to NotFound and
  //                        *key_exists will be set to true.
  // If no key was found, *status will be set to NotFound and
  //                      *key_exists will be set to false.
  // If seq is non-null, *seq will be set to the sequence number found
  // for the key if a key was found.
  //
  // REQUIRES: lock is not held
  void Get(const ReadOptions&, const LookupKey& key, std::string* val,
           Status* status, MergeContext* merge_context,
           bool* value_found = nullptr, bool* key_exists = nullptr,
           SequenceNumber* seq = nullptr);

  // Loads some stats information from files. Call without mutex held. It needs
  // to be called before applying the version to the version set.
  void PrepareApply(const MutableCFOptions& mutable_cf_options,
                    bool update_stats);

  // Reference count management (so Versions do not disappear out from
  // under live iterators)
  void Ref();
  // Decrease reference count. Delete the object if no reference left
  // and return true. Otherwise, return false.
  bool Unref();

  // Add all files listed in the current version to *live.
  void AddLiveFiles(std::vector<FileDescriptor>* live);

  // Return a human readable string that describes this version's contents.
  std::string DebugString(bool hex = false) const;

  // Returns the version nuber of this version
  uint64_t GetVersionNumber() const { return version_number_; }

  // REQUIRES: lock is held
  // On success, "tp" will contains the table properties of the file
  // specified in "file_meta".  If the file name of "file_meta" is
  // known ahread, passing it by a non-null "fname" can save a
  // file-name conversion.
  Status GetTableProperties(std::shared_ptr<const TableProperties>* tp,
                            const FileMetaData* file_meta,
                            const std::string* fname = nullptr) const;

  // REQUIRES: lock is held
  // On success, *props will be populated with all SSTables' table properties.
  // The keys of `props` are the sst file name, the values of `props` are the
  // tables' propertis, represented as shared_ptr.
  Status GetPropertiesOfAllTables(TablePropertiesCollection* props);
  Status GetPropertiesOfAllTables(TablePropertiesCollection* props, int level);
  Status GetPropertiesOfTablesInRange(const Range* range, std::size_t n,
                                      TablePropertiesCollection* props) const;

  // REQUIRES: lock is held
  // On success, "tp" will contains the aggregated table property amoug
  // the table properties of all sst files in this version.
  Status GetAggregatedTableProperties(
      std::shared_ptr<const TableProperties>* tp, int level = -1);

  uint64_t GetEstimatedActiveKeys() {
    return storage_info_.GetEstimatedActiveKeys();
  }

  size_t GetMemoryUsageByTableReaders();

  // Returns weighted middle key of the approximate middle keys of the SST files
  // (see TableReader::GetMiddleKey).
  // Returns Status(Incomplete) if there are no SST files for this version.
  Result<std::string> GetMiddleKey();

  // Returns a table reader for the largest SST file.
  Result<TableReader*> TEST_GetLargestSstTableReader();

  ColumnFamilyData* cfd() const { return cfd_; }

  // Return the next Version in the linked list. Used for debug only
  Version* TEST_Next() const {
    return next_;
  }

  VersionStorageInfo* storage_info() { return &storage_info_; }

  VersionSet* version_set() { return vset_; }

  void GetColumnFamilyMetaData(ColumnFamilyMetaData* cf_meta);

 private:
  Env* env_;
  friend class VersionSet;

  const InternalKeyComparatorPtr& internal_comparator() const {
    return storage_info_.internal_comparator_;
  }
  const Comparator* user_comparator() const {
    return storage_info_.user_comparator_;
  }

  bool PrefixMayMatch(const ReadOptions& read_options,
                      InternalIterator* level_iter,
                      const Slice& internal_prefix) const;

  // Returns true if the filter blocks in the specified level will not be
  // checked during read operations. In certain cases (trivial move or preload),
  // the filter block may already be cached, but we still do not access it such
  // that it eventually expires from the cache.
  bool IsFilterSkipped(int level, bool is_file_last_in_level = false);

  // The helper function of UpdateAccumulatedStats, which may fill the missing
  // fields of file_mata from its associated TableProperties.
  // Returns true if it does initialize FileMetaData.
  bool MaybeInitializeFileMetaData(FileMetaData* file_meta);

  // Update the accumulated stats associated with the current version.
  // This accumulated stats will be used in compaction.
  void UpdateAccumulatedStats(bool update_stats);

  // Sort all files for this version based on their file size and
  // record results in files_by_compaction_pri_. The largest files are listed
  // first.
  void UpdateFilesByCompactionPri();

  Result<TableCache::TableReaderWithHandle> GetLargestSstTableReader();
  Result<std::string> GetMiddleOfMiddleKeys();

  template <typename IteratorBuilder, typename CreateIteratorFunc>
  void AddLevel0Iterators(
      const ReadOptions& read_options,
      const EnvOptions& soptions,
      IteratorBuilder* merge_iter_builder,
      Arena* arena,
      const CreateIteratorFunc& create_iterator_func);

  ColumnFamilyData* cfd_;  // ColumnFamilyData to which this Version belongs
  Logger* info_log_;
  Statistics* db_statistics_;
  TableCache* table_cache_;
  const MergeOperator* merge_operator_;

  VersionStorageInfo storage_info_;
  VersionSet* vset_;            // VersionSet to which this Version belongs
  Version* next_;               // Next version in linked list
  Version* prev_;               // Previous version in linked list
  int refs_;                    // Number of live refs to this version

  // A version number that uniquely represents this version. This is
  // used for debugging and logging purposes only.
  uint64_t version_number_;

  Version(ColumnFamilyData* cfd, VersionSet* vset, uint64_t version_number = 0);

  ~Version();

  // No copying allowed
  Version(const Version&);
  void operator=(const Version&);
};

typedef boost::intrusive_ptr<Version> VersionPtr;

inline void intrusive_ptr_add_ref(Version* version) {
  version->Ref();
}

inline void intrusive_ptr_release(Version* version) {
  version->Unref();
}

class VersionSet {
 public:
  static constexpr uint64_t kInitialNextFileNumber = 2;

  VersionSet(const std::string& dbname, const DBOptions* db_options,
             const EnvOptions& env_options, Cache* table_cache,
             WriteBuffer* write_buffer, WriteController* write_controller);
  ~VersionSet();

  // Apply *edit to the current version to form a new descriptor that
  // is both saved to persistent state and installed as the new
  // current version.  Will release *mu while actually writing to the file.
  // column_family_options has to be set if edit is column family add
  // REQUIRES: *mu is held on entry.
  // REQUIRES: no other thread concurrently calls LogAndApply()
  Status LogAndApply(
      ColumnFamilyData* column_family_data,
      const MutableCFOptions& mutable_cf_options, VersionEdit* edit,
      InstrumentedMutex* mu, Directory* db_directory = nullptr,
      bool new_descriptor_log = false,
      const ColumnFamilyOptions* column_family_options = nullptr);

  // Recover the last saved descriptor from persistent storage.
  // If read_only == true, Recover() will not complain if some column families
  // are not opened
  Status Recover(const std::vector<ColumnFamilyDescriptor>& column_families,
                 bool read_only = false);

  // Reads a manifest file and returns a list of column families in
  // column_families.
  static Status ListColumnFamilies(std::vector<std::string>* column_families,
                                   const std::string& dbname,
                                   BoundaryValuesExtractor* extractor,
                                   Env* env);

  // Try to reduce the number of levels. This call is valid when
  // only one level from the new max level to the old
  // max level containing files.
  // The call is static, since number of levels is immutable during
  // the lifetime of a RocksDB instance. It reduces number of levels
  // in a DB by applying changes to manifest.
  // For example, a db currently has 7 levels [0-6], and a call to
  // to reduce to 5 [0-4] can only be executed when only one level
  // among [4-6] contains files.
  static Status ReduceNumberOfLevels(const std::string& dbname,
                                     const Options* options,
                                     const EnvOptions& env_options,
                                     int new_levels);

  // printf contents (for debugging)
  Status DumpManifest(const Options& options, const std::string& manifestFileName,
                      bool verbose, bool hex = false);


  // Return the current manifest file number
  uint64_t manifest_file_number() const { return manifest_file_number_; }

  uint64_t pending_manifest_file_number() const {
    return pending_manifest_file_number_;
  }

  uint64_t current_next_file_number() const { return next_file_number_.load(); }

  // Allocate and return a new file number
  uint64_t NewFileNumber() { return next_file_number_.fetch_add(1); }

  // Return the last sequence number.
  uint64_t LastSequence() const {
    return last_sequence_.load(std::memory_order_acquire);
  }

  UserFrontier* FlushedFrontier() const {
    return flushed_frontier_.get();
  }

  // Set the last sequence number to s.
  void SetLastSequence(SequenceNumber s);

  // Set last sequence number without verifying that it always keeps increasing.
  void SetLastSequenceNoSanityChecking(SequenceNumber s);

  // Attempts to set the last flushed op id / hybrid time / history cutoff to the specified tuple of
  // values. The current flushed frontier is always updated to the maximum of its current and
  // supplied values for each dimension. This will DFATAL in case the supplied frontier regresses
  // relative to the current frontier in any of its dimensions which have non-default (defined)
  // values.
  void UpdateFlushedFrontier(UserFrontierPtr values);

  // Mark the specified file number as used.
  // REQUIRED: this is only called during single-threaded recovery
  void MarkFileNumberUsedDuringRecovery(uint64_t number);

  // Return the log file number for the log file that is currently
  // being compacted, or zero if there is no such log file.
  uint64_t prev_log_number() const { return prev_log_number_; }

  // Returns the minimum log number such that all
  // log numbers less than or equal to it can be deleted
  uint64_t MinLogNumber() const {
    uint64_t min_log_num = std::numeric_limits<uint64_t>::max();
    for (auto cfd : *column_family_set_) {
      // It's safe to ignore dropped column families here:
      // cfd->IsDropped() becomes true after the drop is persisted in MANIFEST.
      if (min_log_num > cfd->GetLogNumber() && !cfd->IsDropped()) {
        min_log_num = cfd->GetLogNumber();
      }
    }
    return min_log_num;
  }

  // Create an iterator that reads over the compaction inputs for "*c".
  // The caller should delete the iterator when no longer needed.
  InternalIterator* MakeInputIterator(Compaction* c);

  // Add all files listed in any live version to *live.
  void AddLiveFiles(std::vector<FileDescriptor>* live_list);

  // Return the approximate size of data to be scanned for range [start, end)
  // in levels [start_level, end_level). If end_level == 0 it will search
  // through all non-empty levels
  uint64_t ApproximateSize(Version* v, const Slice& start, const Slice& end,
                           int start_level = 0, int end_level = -1);

  // Return the size of the current manifest file
  uint64_t manifest_file_size() const { return manifest_file_size_; }

  // verify that the files that we started with for a compaction
  // still exist in the current version and in the same original level.
  // This ensures that a concurrent compaction did not erroneously
  // pick the same files to compact.
  bool VerifyCompactionFileConsistency(Compaction* c);

  Status GetMetadataForFile(uint64_t number, int* filelevel,
                            FileMetaData** metadata, ColumnFamilyData** cfd);

  // This function doesn't support leveldb SST filenames
  void GetLiveFilesMetaData(std::vector<LiveFileMetaData> *metadata);

  void GetObsoleteFiles(const FileNumbersProvider& pending_outputs,
                        std::vector<FileMetaData*>* files,
                        std::vector<std::string>* manifest_filenames);

  ColumnFamilySet* GetColumnFamilySet() { return column_family_set_.get(); }
  const EnvOptions& env_options() { return env_options_; }

  Status Import(const std::string& source_dir, SequenceNumber seqno, VersionEdit* edit);

  void UnrefFile(ColumnFamilyData* cfd, FileMetaData* f);

  static uint64_t GetNumLiveVersions(Version* dummy_versions);

  static uint64_t GetTotalSstFilesSize(Version* dummy_versions);

  bool has_manifest_writers() const {
    return !manifest_writers_.empty();
  }

 private:
  struct ManifestWriter;

  friend class Version;
  friend class DBImpl;

  // ApproximateSize helper
  uint64_t ApproximateSizeLevel0(Version* v, const LevelFilesBrief& files_brief,
                                 const Slice& start, const Slice& end);

  uint64_t ApproximateSize(Version* v, const FdWithBoundaries& f, const Slice& key);

  // Save current contents to *log
  Status WriteSnapshot(log::Writer* log, UserFrontierPtr flushed_frontier_override);

  void AppendVersion(ColumnFamilyData* column_family_data, Version* v);

  ColumnFamilyData* CreateColumnFamily(const ColumnFamilyOptions& cf_options,
                                       VersionEdit* edit);

  static void EnsureNonDecreasingLastSequence(
      SequenceNumber prev_last_seq, SequenceNumber new_last_seq);
  static void EnsureNonDecreasingFlushedFrontier(
      const UserFrontier* prev_value, const UserFrontier& new_value);

  void UpdateFlushedFrontierNoSanityChecking(UserFrontierPtr values);

  std::unique_ptr<ColumnFamilySet> column_family_set_;

  Env* const env_;
  const std::string dbname_;
  const DBOptions* const db_options_;
  std::atomic<uint64_t> next_file_number_ = {kInitialNextFileNumber};
  uint64_t manifest_file_number_ = 0;
  uint64_t pending_manifest_file_number_ = 0;
  std::atomic<uint64_t> last_sequence_ = {0};
  uint64_t prev_log_number_ = 0; // 0 or backing store for memtable being compacted
  UserFrontierPtr flushed_frontier_;

  // Opened lazily
  std::unique_ptr<log::Writer> descriptor_log_;
  std::string descriptor_log_file_name_;

  // generates a increasing version number for every new version
  uint64_t current_version_number_ = 0;

  // Queue of writers to the manifest file
  std::deque<ManifestWriter*> manifest_writers_;

  // Current size of manifest file
  uint64_t manifest_file_size_ = 0;

  std::vector<FileMetaData*> obsolete_files_;
  std::vector<std::string> obsolete_manifests_;

  // env options for all reads and writes except compactions
  const EnvOptions& env_options_;

  // env options used for compactions. This is a copy of
  // env_options_ but with readaheads set to readahead_compactions_.
  const EnvOptions env_options_compactions_;

  // No copying allowed
  VersionSet(const VersionSet&);
  void operator=(const VersionSet&);

  void LogAndApplyCFHelper(VersionEdit* edit);
  void LogAndApplyHelper(
      ColumnFamilyData* cfd,
      VersionBuilder* b,
      VersionEdit* edit,
      InstrumentedMutex* mu);
};

}  // namespace rocksdb
