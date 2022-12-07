// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#pragma once

#include <deque>
#include <string>
#include <vector>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/utilities/utility_db.h"
#include "yb/rocksdb/utilities/db_ttl.h"
#include "yb/rocksdb/db/db_impl.h"

#ifdef _WIN32
// Windows API macro interference
#undef GetCurrentTime
#endif


namespace rocksdb {

class DBWithTTLImpl : public DBWithTTL {
 public:
  static void SanitizeOptions(int32_t ttl, ColumnFamilyOptions* options,
                              Env* env);

  explicit DBWithTTLImpl(DB* db);

  virtual ~DBWithTTLImpl();

  Status CreateColumnFamilyWithTtl(const ColumnFamilyOptions& options,
                                   const std::string& column_family_name,
                                   ColumnFamilyHandle** handle,
                                   int ttl) override;

  Status CreateColumnFamily(const ColumnFamilyOptions& options,
                            const std::string& column_family_name,
                            ColumnFamilyHandle** handle) override;

  using StackableDB::Put;
  virtual Status Put(const WriteOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     const Slice& val) override;

  using StackableDB::Get;
  virtual Status Get(const ReadOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     std::string* value) override;

  using StackableDB::MultiGet;
  virtual std::vector<Status> MultiGet(
      const ReadOptions& options,
      const std::vector<ColumnFamilyHandle*>& column_family,
      const std::vector<Slice>& keys,
      std::vector<std::string>* values) override;

  using StackableDB::KeyMayExist;
  virtual bool KeyMayExist(const ReadOptions& options,
                           ColumnFamilyHandle* column_family, const Slice& key,
                           std::string* value,
                           bool* value_found = nullptr) override;

  using StackableDB::Merge;
  virtual Status Merge(const WriteOptions& options,
                       ColumnFamilyHandle* column_family, const Slice& key,
                       const Slice& value) override;

  virtual Status Write(const WriteOptions& opts, WriteBatch* updates) override;

  using StackableDB::NewIterator;
  virtual Iterator* NewIterator(const ReadOptions& opts,
                                ColumnFamilyHandle* column_family) override;

  virtual DB* GetBaseDB() override { return db_; }

  static bool IsStale(const Slice& value, int32_t ttl, Env* env);

  static Status AppendTS(const Slice& val, std::string* val_with_ts, Env* env);

  static Status SanityCheckTimestamp(const Slice& str);

  static Status StripTS(std::string* str);

  static const uint32_t kTSLength = sizeof(int32_t);  // size of timestamp

  static const int32_t kMinTimestamp = 1368146402;  // 05/09/2013:5:40PM GMT-8

  static const int32_t kMaxTimestamp = 2147483647;  // 01/18/2038:7:14PM GMT-8
};

class TtlIterator : public Iterator {

 public:
  explicit TtlIterator(Iterator* iter) : iter_(iter) { assert(iter_); }

  ~TtlIterator() { delete iter_; }

  bool Valid() const override { return iter_->Valid(); }

  void SeekToFirst() override { iter_->SeekToFirst(); }

  void SeekToLast() override { iter_->SeekToLast(); }

  void Seek(const Slice& target) override { iter_->Seek(target); }

  void Next() override { iter_->Next(); }

  void Prev() override { iter_->Prev(); }

  Slice key() const override { return iter_->key(); }

  int32_t timestamp() const {
    return DecodeFixed32(iter_->value().data() + iter_->value().size() -
                         DBWithTTLImpl::kTSLength);
  }

  Slice value() const override {
    // TODO: handle timestamp corruption like in general iterator semantics
    assert(DBWithTTLImpl::SanityCheckTimestamp(iter_->value()).ok());
    Slice trimmed_value = iter_->value();
    trimmed_value.remove_suffix(DBWithTTLImpl::kTSLength);
    return trimmed_value;
  }

  Status status() const override { return iter_->status(); }

 private:
  Iterator* iter_;
};

class TtlCompactionFilter : public CompactionFilter {
 public:
  TtlCompactionFilter(
      int32_t ttl, Env* env, CompactionFilter* user_comp_filter,
      std::unique_ptr<CompactionFilter> user_comp_filter_from_factory = nullptr)
      : ttl_(ttl),
        env_(env),
        user_comp_filter_(user_comp_filter),
        user_comp_filter_from_factory_(
            std::move(user_comp_filter_from_factory)) {
    // Unlike the merge operator, compaction filter is necessary for TTL, hence
    // this would be called even if user doesn't specify any compaction-filter
    if (!user_comp_filter_) {
      user_comp_filter_ = user_comp_filter_from_factory_.get();
    }
  }

  FilterDecision Filter(int level, const Slice& key, const Slice& old_val,
                        std::string* new_val, bool* value_changed) override {
    if (DBWithTTLImpl::IsStale(old_val, ttl_, env_)) {
      return FilterDecision::kDiscard;
    }
    if (user_comp_filter_ == nullptr) {
      return FilterDecision::kKeep;
    }
    assert(old_val.size() >= DBWithTTLImpl::kTSLength);
    Slice old_val_without_ts(old_val.data(),
                             old_val.size() - DBWithTTLImpl::kTSLength);
    if (user_comp_filter_->Filter(level, key, old_val_without_ts, new_val,
                                  value_changed) != FilterDecision::kKeep) {
      return FilterDecision::kDiscard;
    }
    if (*value_changed) {
      new_val->append(old_val.cend() - DBWithTTLImpl::kTSLength, DBWithTTLImpl::kTSLength);
    }
    return FilterDecision::kKeep;
  }

  virtual const char* Name() const override { return "Delete By TTL"; }

 private:
  int32_t ttl_;
  Env* env_;
  CompactionFilter* user_comp_filter_;
  std::unique_ptr<CompactionFilter> user_comp_filter_from_factory_;
};

class TtlCompactionFilterFactory : public CompactionFilterFactory {
 public:
  TtlCompactionFilterFactory(
      int32_t ttl, Env* env,
      std::shared_ptr<CompactionFilterFactory> comp_filter_factory)
      : ttl_(ttl), env_(env), user_comp_filter_factory_(comp_filter_factory) {}

  std::unique_ptr<CompactionFilter> CreateCompactionFilter(
      const CompactionFilter::Context& context) override {
    std::unique_ptr<CompactionFilter> user_comp_filter_from_factory;
    if (user_comp_filter_factory_) {
      user_comp_filter_from_factory =
          user_comp_filter_factory_->CreateCompactionFilter(context);
    }

    return std::make_unique<TtlCompactionFilter>(
        ttl_, env_, nullptr, std::move(user_comp_filter_from_factory));
  }

  virtual const char* Name() const override {
    return "TtlCompactionFilterFactory";
  }

 private:
  int32_t ttl_;
  Env* env_;
  std::shared_ptr<CompactionFilterFactory> user_comp_filter_factory_;
};

class TtlMergeOperator : public MergeOperator {

 public:
  explicit TtlMergeOperator(const std::shared_ptr<MergeOperator>& merge_op,
                            Env* env)
      : user_merge_op_(merge_op), env_(env) {
    assert(merge_op);
    assert(env);
  }

  virtual bool FullMerge(const Slice& key, const Slice* existing_value,
                         const std::deque<std::string>& operands,
                         std::string* new_value, Logger* logger) const
      override {
    constexpr uint32_t kTsLen = DBWithTTLImpl::kTSLength;
    if (existing_value && existing_value->size() < kTsLen) {
      RLOG(InfoLogLevel::ERROR_LEVEL, logger,
          "Error: Could not remove timestamp from existing value.");
      return false;
    }

    // Extract time-stamp from each operand to be passed to user_merge_op_
    std::deque<std::string> operands_without_ts;
    for (const auto& operand : operands) {
      if (operand.size() < kTsLen) {
        RLOG(InfoLogLevel::ERROR_LEVEL, logger,
            "Error: Could not remove timestamp from operand value.");
        return false;
      }
      operands_without_ts.push_back(operand.substr(0, operand.size() - kTsLen));
    }

    // Apply the user merge operator (store result in *new_value)
    bool good = true;
    if (existing_value) {
      Slice existing_value_without_ts(existing_value->data(),
                                      existing_value->size() - kTsLen);
      good = user_merge_op_->FullMerge(key, &existing_value_without_ts,
                                       operands_without_ts, new_value, logger);
    } else {
      good = user_merge_op_->FullMerge(key, nullptr, operands_without_ts,
                                       new_value, logger);
    }

    // Return false if the user merge operator returned false
    if (!good) {
      return false;
    }

    // Augment the *new_value with the ttl time-stamp
    int64_t curtime;
    if (!env_->GetCurrentTime(&curtime).ok()) {
      RLOG(InfoLogLevel::ERROR_LEVEL, logger,
          "Error: Could not get current time to be attached internally "
          "to the new value.");
      return false;
    } else {
      char ts_string[kTsLen];
      EncodeFixed32(ts_string, static_cast<uint32_t>(curtime));
      new_value->append(ts_string, kTsLen);
      return true;
    }
  }

  virtual bool PartialMergeMulti(const Slice& key,
                                 const std::deque<Slice>& operand_list,
                                 std::string* new_value, Logger* logger) const
      override {
    constexpr uint32_t kTsLen = DBWithTTLImpl::kTSLength;
    std::deque<Slice> operands_without_ts;

    for (const auto& operand : operand_list) {
      if (operand.size() < kTsLen) {
        RLOG(InfoLogLevel::ERROR_LEVEL, logger,
            "Error: Could not remove timestamp from value.");
        return false;
      }

      operands_without_ts.push_back(
          Slice(operand.data(), operand.size() - kTsLen));
    }

    // Apply the user partial-merge operator (store result in *new_value)
    assert(new_value);
    if (!user_merge_op_->PartialMergeMulti(key, operands_without_ts, new_value,
                                           logger)) {
      return false;
    }

    // Augment the *new_value with the ttl time-stamp
    int64_t curtime;
    if (!env_->GetCurrentTime(&curtime).ok()) {
      RLOG(InfoLogLevel::ERROR_LEVEL, logger,
          "Error: Could not get current time to be attached internally "
          "to the new value.");
      return false;
    } else {
      char ts_string[kTsLen];
      EncodeFixed32(ts_string, static_cast<uint32_t>(curtime));
      new_value->append(ts_string, kTsLen);
      return true;
    }
  }

  virtual const char* Name() const override { return "Merge By TTL"; }

 private:
  std::shared_ptr<MergeOperator> user_merge_op_;
  Env* env_;
};
} // namespace rocksdb
