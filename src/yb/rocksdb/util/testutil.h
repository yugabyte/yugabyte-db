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
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/casts.h"

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/db/version_edit.pb.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/plain_table_factory.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/random.h"

#include "yb/util/slice.h"

DECLARE_bool(never_fsync);
namespace rocksdb {
class SequentialFileReader;

class RocksDBTest : public ::testing::Test {
 public:
  RocksDBTest() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = true;
  }
};

namespace test {

extern std::string RandomHumanReadableString(Random* rnd, int len);

// Return a random key with the specified length that may contain interesting
// characters (e.g. \x00, \xff, etc.).
enum RandomKeyType : char { RANDOM, LARGEST, SMALLEST, MIDDLE };
extern std::string RandomKey(Random* rnd, int len,
                             RandomKeyType type = RandomKeyType::RANDOM);

// A wrapper that allows injection of errors.
class ErrorEnv : public EnvWrapper {
 public:
  bool writable_file_error_;
  int num_writable_file_errors_;

  ErrorEnv() : EnvWrapper(Env::Default()),
               writable_file_error_(false),
               num_writable_file_errors_(0) { }

  virtual Status NewWritableFile(const std::string& fname,
                                 std::unique_ptr<WritableFile>* result,
                                 const EnvOptions& soptions) override {
    result->reset();
    if (writable_file_error_) {
      ++num_writable_file_errors_;
      return STATUS(IOError, fname, "fake error");
    }
    return target()->NewWritableFile(fname, result, soptions);
  }
};

// An internal comparator that just forward comparing results from the
// user comparator in it. Can be used to test entities that have no dependency
// on internal key structure but consumes InternalKeyComparator, like
// BlockBasedTable.
class PlainInternalKeyComparator : public InternalKeyComparator {
 public:
  explicit PlainInternalKeyComparator(const Comparator* c)
      : InternalKeyComparator(c) {}

  virtual ~PlainInternalKeyComparator() {}

  virtual int Compare(Slice a, Slice b) const override {
    return user_comparator()->Compare(a, b);
  }
  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const override {
    user_comparator()->FindShortestSeparator(start, limit);
  }
  virtual void FindShortSuccessor(std::string* key) const override {
    user_comparator()->FindShortSuccessor(key);
  }
};

// A test comparator which compare two strings in this way:
// (1) first compare prefix of 8 bytes in alphabet order,
// (2) if two strings share the same prefix, sort the other part of the string
//     in the reverse alphabet order.
// This helps simulate the case of compounded key of [entity][timestamp] and
// latest timestamp first.
class SimpleSuffixReverseComparator : public Comparator {
 public:
  SimpleSuffixReverseComparator() {}

  virtual const char* Name() const override {
    return "SimpleSuffixReverseComparator";
  }

  virtual int Compare(Slice a, Slice b) const override {
    Slice prefix_a = Slice(a.data(), 8);
    Slice prefix_b = Slice(b.data(), 8);
    int prefix_comp = prefix_a.compare(prefix_b);
    if (prefix_comp != 0) {
      return prefix_comp;
    } else {
      Slice suffix_a = Slice(a.data() + 8, a.size() - 8);
      Slice suffix_b = Slice(b.data() + 8, b.size() - 8);
      return -(suffix_a.compare(suffix_b));
    }
  }
  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const override {}

  virtual void FindShortSuccessor(std::string* key) const override {}
};

// Iterator over a vector of keys/values
class VectorIterator : public InternalIterator {
 public:
  explicit VectorIterator(const std::vector<std::string>& keys)
      : keys_(keys), current_(keys.size()) {
    std::sort(keys_.begin(), keys_.end());
    values_.resize(keys.size());
  }

  VectorIterator(const std::vector<std::string>& keys,
      const std::vector<std::string>& values)
    : keys_(keys), values_(values), current_(keys.size()) {
    assert(keys_.size() == values_.size());
  }

  const KeyValueEntry& SeekToFirst() override {
    current_ = 0;
    return Entry();
  }

  const KeyValueEntry& SeekToLast() override {
    current_ = keys_.size() - 1;
    return Entry();
  }

  const KeyValueEntry& Seek(Slice target) override {
    current_ = std::lower_bound(keys_.begin(), keys_.end(), target.ToBuffer()) - keys_.begin();
    return Entry();
  }

  const KeyValueEntry& Next() override {
    current_++;
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    current_--;
    return Entry();
  }

  const KeyValueEntry& Entry() const override {
    if (current_ >= keys_.size()) {
      return KeyValueEntry::Invalid();
    }
    entry_ = KeyValueEntry {
      .key = Slice(keys_[current_]),
      .value = Slice(values_[current_]),
    };
    return entry_;
  }

  Status status() const override { return Status::OK(); }

 private:
  std::vector<std::string> keys_;
  std::vector<std::string> values_;
  size_t current_;
  mutable KeyValueEntry entry_;
};

extern WritableFileWriter* GetWritableFileWriter(WritableFile* wf);

extern RandomAccessFileReader* GetRandomAccessFileReader(RandomAccessFile* raf);

extern SequentialFileReader* GetSequentialFileReader(SequentialFile* se);

class StringSink: public WritableFile {
 public:
  std::string contents_;

  explicit StringSink(Slice* reader_contents = nullptr) :
      WritableFile(),
      contents_(""),
      reader_contents_(reader_contents),
      last_flush_(0) {
    if (reader_contents_ != nullptr) {
      *reader_contents_ = Slice(contents_.data(), 0UL);
    }
  }

  const std::string& contents() const { return contents_; }

  virtual Status Truncate(uint64_t size) override {
    contents_.resize(static_cast<size_t>(size));
    return Status::OK();
  }
  virtual Status Close() override { return Status::OK(); }
  virtual Status Flush() override {
    if (reader_contents_ != nullptr) {
      assert(reader_contents_->size() <= last_flush_);
      size_t offset = last_flush_ - reader_contents_->size();
      *reader_contents_ = Slice(
          contents_.data() + offset,
          contents_.size() - offset);
      last_flush_ = contents_.size();
    }

    return Status::OK();
  }
  virtual Status Sync() override { return Status::OK(); }
  virtual Status Append(const Slice& slice) override {
    contents_.append(slice.cdata(), slice.size());
    return Status::OK();
  }
  void Drop(size_t bytes) {
    if (reader_contents_ != nullptr) {
      contents_.resize(contents_.size() - bytes);
      *reader_contents_ = Slice(
          reader_contents_->data(), reader_contents_->size() - bytes);
      last_flush_ = contents_.size();
    }
  }

  const std::string& filename() const override {
    static const std::string kFilename = "StringSink";
    return kFilename;
  }

 private:
  Slice* reader_contents_;
  size_t last_flush_;
};

class StringSource: public RandomAccessFile {
 public:
  explicit StringSource(const Slice& contents, uint64_t uniq_id = 0,
                        bool mmap = false)
      : contents_(contents.cdata(), contents.size()),
        uniq_id_(uniq_id),
        mmap_(mmap),
        total_reads_(0) {}

  virtual ~StringSource() {}

  yb::Result<uint64_t> Size() const override { return contents_.size(); }

  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override {
    total_reads_++;
    if (offset > contents_.size()) {
      return STATUS(InvalidArgument, "invalid Read offset");
    }
    if (offset + n > contents_.size()) {
      n = contents_.size() - static_cast<size_t>(offset);
    }
    if (!mmap_) {
      memcpy(scratch, &contents_[static_cast<size_t>(offset)], n);
      *result = Slice(scratch, n);
    } else {
      *result = Slice(&contents_[static_cast<size_t>(offset)], n);
    }
    return Status::OK();
  }

  virtual size_t GetUniqueId(char* id) const override {
    char* rid = id;
    rid = EncodeVarint64(rid, uniq_id_);
    rid = EncodeVarint64(rid, 0);
    return static_cast<size_t>(rid-id);
  }

  yb::Result<uint64_t> INode() const override { return STATUS(NotSupported, "Not supported"); }

  const std::string& filename() const override { return filename_; }

  size_t memory_footprint() const override { LOG(FATAL) << "Not supported"; }

  int total_reads() const { return total_reads_; }

  void set_total_reads(int tr) { total_reads_ = tr; }

 private:
  std::string filename_ = "StringSource";
  std::string contents_;
  uint64_t uniq_id_;
  bool mmap_;
  mutable int total_reads_;
};

class NullLogger : public Logger {
 public:
  using Logger::Logv;
  virtual void Logv(const char* format, va_list ap) override {}
  virtual size_t GetLogFileSize() const override { return 0; }
};

// Corrupts key by changing the type
extern void CorruptKeyType(InternalKey* ikey);

extern std::string KeyStr(const std::string& user_key,
                          const SequenceNumber& seq, const ValueType& t,
                          bool corrupt = false);

class SleepingBackgroundTask {
 public:
  SleepingBackgroundTask()
      : bg_cv_(&mutex_),
        should_sleep_(true),
        done_with_sleep_(false),
        sleeping_(false) {}

  bool IsSleeping() {
    MutexLock l(&mutex_);
    return sleeping_;
  }
  void DoSleep() {
    MutexLock l(&mutex_);
    sleeping_ = true;
    bg_cv_.SignalAll();
    while (should_sleep_) {
      bg_cv_.Wait();
    }
    sleeping_ = false;
    done_with_sleep_ = true;
    bg_cv_.SignalAll();
  }
  void WaitUntilSleeping() {
    MutexLock l(&mutex_);
    while (!sleeping_ || !should_sleep_) {
      bg_cv_.Wait();
    }
  }
  void WakeUp() {
    MutexLock l(&mutex_);
    should_sleep_ = false;
    bg_cv_.SignalAll();
  }
  void WaitUntilDone() {
    MutexLock l(&mutex_);
    while (!done_with_sleep_) {
      bg_cv_.Wait();
    }
  }
  bool WokenUp() {
    MutexLock l(&mutex_);
    return should_sleep_ == false;
  }

  void Reset() {
    MutexLock l(&mutex_);
    should_sleep_ = true;
    done_with_sleep_ = false;
  }

  static void DoSleepTask(void* arg) {
    reinterpret_cast<SleepingBackgroundTask*>(arg)->DoSleep();
  }

 private:
  port::Mutex mutex_;
  port::CondVar bg_cv_;  // Signalled when background work finishes
  bool should_sleep_;
  bool done_with_sleep_;
  bool sleeping_;
};

// Filters merge operands and values that are equal to `num`.
class FilterNumber : public CompactionFilter {
 public:
  explicit FilterNumber(uint64_t num) : num_(num) {}

  std::string last_merge_operand_key() { return last_merge_operand_key_; }

  FilterDecision Filter(int level, const rocksdb::Slice& key, const rocksdb::Slice& value,
              std::string* new_value, bool* value_changed) override {
    if (value.size() == sizeof(uint64_t)) {
      return num_ == DecodeFixed64(value.data()) ? FilterDecision::kDiscard : FilterDecision::kKeep;
    }
    return FilterDecision::kDiscard;
  }

  bool FilterMergeOperand(int level, const rocksdb::Slice& key,
                          const rocksdb::Slice& value) const override {
    last_merge_operand_key_ = key.ToString();
    if (value.size() == sizeof(uint64_t)) {
      return num_ == DecodeFixed64(value.data());
    }
    return true;
  }

  const char* Name() const override { return "FilterBadMergeOperand"; }

 private:
  mutable std::string last_merge_operand_key_;
  uint64_t num_;
};

inline std::string EncodeInt(uint64_t x) {
  std::string result;
  PutFixed64(&result, x);
  return result;
}

class StringEnv : public EnvWrapper {
 public:
  class SeqStringSource : public SequentialFile {
   public:
    explicit SeqStringSource(const std::string& data)
        : data_(data), offset_(0) {}

    ~SeqStringSource() {}

    Status Read(size_t n, Slice* result, uint8_t* scratch) override {
      std::string output;
      if (offset_ < data_.size()) {
        n = std::min(data_.size() - offset_, n);
        memcpy(scratch, data_.data() + offset_, n);
        offset_ += n;
        *result = Slice(scratch, n);
      } else {
        return STATUS(InvalidArgument,
            "Attemp to read when it already reached eof.");
      }
      return Status::OK();
    }

    Status Skip(uint64_t n) override {
      if (offset_ >= data_.size()) {
        return STATUS(InvalidArgument,
            "Attemp to read when it already reached eof.");
      }
      // TODO(yhchiang): Currently doesn't handle the overflow case.
      offset_ += n;
      return Status::OK();
    }

    const std::string& filename() const override {
      static const std::string kFilename = "SeqStringSource";
      return kFilename;
    }

   private:
    std::string data_;
    size_t offset_;
  };

  class StringSink : public WritableFile {
   public:
    explicit StringSink(std::string* contents)
        : WritableFile(), contents_(contents) {}
    virtual Status Truncate(uint64_t size) override {
      contents_->resize(size);
      return Status::OK();
    }
    virtual Status Close() override { return Status::OK(); }
    virtual Status Flush() override { return Status::OK(); }
    virtual Status Sync() override { return Status::OK(); }
    virtual Status Append(const Slice& slice) override {
      contents_->append(slice.cdata(), slice.size());
      return Status::OK();
    }

    const std::string& filename() const override {
      static const std::string kFilename = "StringSink";
      return kFilename;
    }

   private:
    std::string* contents_;
  };

  explicit StringEnv(Env* t) : EnvWrapper(t) {}
  virtual ~StringEnv() {}

  const std::string& GetContent(const std::string& f) { return files_[f]; }

  const Status WriteToNewFile(const std::string& file_name,
                              const std::string& content) {
    std::unique_ptr<WritableFile> r;
    auto s = NewWritableFile(file_name, &r, EnvOptions());
    if (!s.ok()) {
      return s;
    }
    RETURN_NOT_OK(r->Append(content));
    RETURN_NOT_OK(r->Flush());
    RETURN_NOT_OK(r->Close());
    assert(files_[file_name] == content);
    return Status::OK();
  }

  // The following text is boilerplate that forwards all methods to target()
  Status NewSequentialFile(const std::string& f, std::unique_ptr<SequentialFile>* r,
                           const EnvOptions& options) override {
    auto iter = files_.find(f);
    if (iter == files_.end()) {
      return STATUS(NotFound, "The specified file does not exist", f);
    }
    r->reset(new SeqStringSource(iter->second));
    return Status::OK();
  }
  Status NewRandomAccessFile(const std::string& f,
                             std::unique_ptr<RandomAccessFile>* r,
                             const EnvOptions& options) override {
    return STATUS(NotSupported, "");
  }
  Status NewWritableFile(const std::string& f, std::unique_ptr<WritableFile>* r,
                         const EnvOptions& options) override {
    auto iter = files_.find(f);
    if (iter != files_.end()) {
      return STATUS(IOError, "The specified file already exists", f);
    }
    r->reset(new StringSink(&files_[f]));
    return Status::OK();
  }
  virtual Status NewDirectory(const std::string& name,
                              std::unique_ptr<Directory>* result) override {
    return STATUS(NotSupported, "");
  }
  Status FileExists(const std::string& f) override {
    if (files_.find(f) == files_.end()) {
      return STATUS(NotFound, "");
    }
    return Status::OK();
  }
  Status GetChildren(const std::string& dir,
                     std::vector<std::string>* r) override {
    return STATUS(NotSupported, "");
  }
  Status DeleteFile(const std::string& f) override {
    files_.erase(f);
    return Status::OK();
  }
  Status CreateDir(const std::string& d) override {
    return STATUS(NotSupported, "");
  }
  Status CreateDirIfMissing(const std::string& d) override {
    return STATUS(NotSupported, "");
  }
  Status DeleteDir(const std::string& d) override {
    return STATUS(NotSupported, "");
  }
  Status GetFileSize(const std::string& f, uint64_t* s) override {
    auto iter = files_.find(f);
    if (iter == files_.end()) {
      return STATUS(NotFound, "The specified file does not exist:", f);
    }
    *s = iter->second.size();
    return Status::OK();
  }

  Status GetFileModificationTime(const std::string& fname,
                                 uint64_t* file_mtime) override {
    return STATUS(NotSupported, "");
  }

  Status RenameFile(const std::string& s, const std::string& t) override {
    return STATUS(NotSupported, "");
  }

  Status LinkFile(const std::string& s, const std::string& t) override {
    return STATUS(NotSupported, "");
  }

  Status LockFile(const std::string& f, FileLock** l) override {
    return STATUS(NotSupported, "");
  }

  Status UnlockFile(FileLock* l) override { return STATUS(NotSupported, ""); }

 protected:
  std::unordered_map<std::string, std::string> files_;
};

// Randomly initialize the given DBOptions
void RandomInitDBOptions(DBOptions* db_opt, Random* rnd);

// Randomly initialize the given ColumnFamilyOptions
// Note that the caller is responsible for releasing non-null
// cf_opt->compaction_filter.
void RandomInitCFOptions(ColumnFamilyOptions* cf_opt, Random* rnd);

// A dummy merge operator which can change its name
class ChanglingMergeOperator : public MergeOperator {
 public:
  explicit ChanglingMergeOperator(const std::string& name)
      : name_(name + "MergeOperator") {}
  ~ChanglingMergeOperator() {}

  void SetName(const std::string& name) { name_ = name; }

  virtual bool FullMerge(const Slice& key, const Slice* existing_value,
                         const std::deque<std::string>& operand_list,
                         std::string* new_value,
                         Logger* logger) const override {
    return false;
  }
  virtual bool PartialMergeMulti(const Slice& key,
                                 const std::deque<Slice>& operand_list,
                                 std::string* new_value,
                                 Logger* logger) const override {
    return false;
  }
  virtual const char* Name() const override { return name_.c_str(); }

 protected:
  std::string name_;
};

// Returns a dummy merge operator with random name.
MergeOperator* RandomMergeOperator(Random* rnd);

// A dummy compaction filter which can change its name
class ChanglingCompactionFilter : public CompactionFilter {
 public:
  explicit ChanglingCompactionFilter(const std::string& name)
      : name_(name + "CompactionFilter") {}
  ~ChanglingCompactionFilter() {}

  void SetName(const std::string& name) { name_ = name; }

  FilterDecision Filter(
      int level, const Slice& key, const Slice& existing_value, std::string* new_value,
      bool* value_changed) override {
    return FilterDecision::kKeep;
  }

  const char* Name() const override { return name_.c_str(); }

 private:
  std::string name_;
};

// Returns a dummy compaction filter with a random name.
CompactionFilter* RandomCompactionFilter(Random* rnd);

// A dummy compaction filter factory which can change its name
class ChanglingCompactionFilterFactory : public CompactionFilterFactory {
 public:
  explicit ChanglingCompactionFilterFactory(const std::string& name)
      : name_(name + "CompactionFilterFactory") {}
  ~ChanglingCompactionFilterFactory() {}

  void SetName(const std::string& name) { name_ = name; }

  std::unique_ptr<CompactionFilter> CreateCompactionFilter(
      const CompactionFilter::Context& context) override {
    return std::unique_ptr<CompactionFilter>();
  }

  // Returns a name that identifies this compaction filter factory.
  const char* Name() const override { return name_.c_str(); }

 protected:
  std::string name_;
};

CompressionType RandomCompressionType(Random* rnd);

void RandomCompressionTypeVector(const size_t count,
                                 std::vector<CompressionType>* types,
                                 Random* rnd);

CompactionFilterFactory* RandomCompactionFilterFactory(Random* rnd);

const SliceTransform* RandomSliceTransform(Random* rnd, int pre_defined = -1);

TableFactory* RandomTableFactory(Random* rnd, int pre_defined = -1);

std::string RandomName(Random* rnd, const size_t len);

std::shared_ptr<BoundaryValuesExtractor> MakeBoundaryValuesExtractor();
UserBoundaryValue MakeLeftBoundaryValue(const Slice& value);
UserBoundaryValue MakeRightBoundaryValue(const Slice& value);
Slice GetBoundaryLeft(const UserBoundaryValues& values);
Slice GetBoundaryRight(const UserBoundaryValues& values);

struct BoundaryTestValues {
  void Feed(Slice key);
  void Check(const FileBoundaryValues<InternalKey>& smallest,
             const FileBoundaryValues<InternalKey>& largest);

  UserBoundaryValue::Value min_left;
  UserBoundaryValue::Value max_left;
  UserBoundaryValue::Value min_right;
  UserBoundaryValue::Value max_right;
};

// A test implementation of UserFrontier, wrapper over simple int64_t value.
class TestUserFrontier : public UserFrontier {
 public:
  TestUserFrontier() : value_(0) {}
  explicit TestUserFrontier(uint64_t value) : value_(value) {}

  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<TestUserFrontier>(*this);
  }

  void SetValue(uint64_t value) {
    value_ = value;
  }

  uint64_t Value() const {
    return value_;
  }

  std::string ToString() const override;

  void ToPB(google::protobuf::Any* pb) const override {
    UserBoundaryValuePB value;
    value.set_tag(static_cast<uint32_t>(value_));
    pb->PackFrom(value);
  }

  bool Equals(const UserFrontier& rhs) const override {
    return value_ == down_cast<const TestUserFrontier&>(rhs).value_;
  }

  void Update(const UserFrontier& rhs, UpdateUserValueType type) override {
    auto rhs_value = down_cast<const TestUserFrontier&>(rhs).value_;
    switch (type) {
      case UpdateUserValueType::kLargest:
        value_ = std::max(value_, rhs_value);
        return;
      case UpdateUserValueType::kSmallest:
        value_ = std::min(value_, rhs_value);
        return;
    }
    FATAL_INVALID_ENUM_VALUE(UpdateUserValueType, type);
  }

  bool IsUpdateValid(const UserFrontier& rhs, UpdateUserValueType type) const override {
    auto rhs_value = down_cast<const TestUserFrontier&>(rhs).value_;
    switch (type) {
      case UpdateUserValueType::kLargest:
        return rhs_value >= value_;
      case UpdateUserValueType::kSmallest:
        return rhs_value <= value_;
    }
    FATAL_INVALID_ENUM_VALUE(UpdateUserValueType, type);
  }

  void FromOpIdPBDeprecated(const yb::OpIdPB& op_id) override {}

  Status FromPB(const google::protobuf::Any& pb) override {
    UserBoundaryValuePB value;
    pb.UnpackTo(&value);
    value_ = value.tag();
    return Status::OK();
  }

  Slice FilterAsSlice() override {
    return Slice();
  }

  void ResetFilter() override {}

 private:
  uint64_t value_ = 0;
};

class TestUserFrontiers : public rocksdb::UserFrontiersBase<TestUserFrontier> {
 public:
  TestUserFrontiers(uint64_t min, uint64_t max) {
    Smallest().SetValue(min);
    Largest().SetValue(max);
  }

  std::unique_ptr<UserFrontiers> Clone() const {
    return std::make_unique<TestUserFrontiers>(*this);
  }
};

// A class which remembers the name of each flushed file.
class FlushedFileCollector : public EventListener {
 public:
  virtual void OnFlushCompleted(DB* db, const FlushJobInfo& info) override {
    std::lock_guard lock(mutex_);
    flushed_file_infos_.push_back(info);
  }

  std::vector<std::string> GetFlushedFiles() {
    std::vector<std::string> flushed_files;
    std::lock_guard lock(mutex_);
    for (const auto& info : flushed_file_infos_) {
      flushed_files.push_back(info.file_path);
    }
    return flushed_files;
  }

  std::vector<std::string> GetAndClearFlushedFiles() {
    std::vector<std::string> flushed_files;
    std::lock_guard lock(mutex_);
    for (const auto& info : flushed_file_infos_) {
      flushed_files.push_back(info.file_path);
    }
    flushed_file_infos_.clear();
    return flushed_files;
  }

  std::vector<FlushJobInfo> GetFlushedFileInfos() {
    std::lock_guard lock(mutex_);
    return flushed_file_infos_;
  }

  void Clear() {
    std::lock_guard lock(mutex_);
    flushed_file_infos_.clear();
  }

 private:
  std::vector<FlushJobInfo> flushed_file_infos_;
  std::mutex mutex_;
};

}  // namespace test
}  // namespace rocksdb
