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

#include "yb/rocksdb/db/compacted_db_impl.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/version_set.h"

#include "yb/rocksdb/table/get_context.h"
#include "yb/rocksdb/table/table_reader.h"

namespace rocksdb {

extern void MarkKeyMayExist(void* arg);
extern bool SaveValue(void* arg, const ParsedInternalKey& parsed_key,
                      const Slice& v, bool hit_and_return);

CompactedDBImpl::CompactedDBImpl(
  const DBOptions& options, const std::string& dbname)
  : DBImpl(options, dbname) {
}

CompactedDBImpl::~CompactedDBImpl() {
}

size_t CompactedDBImpl::FindFile(const Slice& key) {
  size_t left = 0;
  size_t right = files_.num_files - 1;
  while (left < right) {
    size_t mid = (left + right) >> 1;
    const FdWithBoundaries& f = files_.files[mid];
    if (user_comparator_->Compare(ExtractUserKey(f.largest.key), key) < 0) {
      // Key at "mid.largest" is < "target".  Therefore all
      // files at or before "mid" are uninteresting.
      left = mid + 1;
    } else {
      // Key at "mid.largest" is >= "target".  Therefore all files
      // after "mid" are uninteresting.
      right = mid;
    }
  }
  return right;
}

Status CompactedDBImpl::Get(const ReadOptions& options,
     ColumnFamilyHandle*, const Slice& key, std::string* value) {
  GetContext get_context(user_comparator_, nullptr, nullptr, nullptr,
                         GetContext::kNotFound, key, value, nullptr, nullptr,
                         nullptr);
  LookupKey lkey(key, kMaxSequenceNumber);
  RETURN_NOT_OK(files_.files[FindFile(key)].fd.table_reader->Get(
      options, lkey.internal_key(), &get_context));
  if (get_context.State() == GetContext::kFound) {
    return Status::OK();
  }
  return STATUS(NotFound, "");
}

std::vector<Status> CompactedDBImpl::MultiGet(const ReadOptions& options,
    const std::vector<ColumnFamilyHandle*>&,
    const std::vector<Slice>& keys, std::vector<std::string>* values) {
  autovector<TableReader*, 16> reader_list;
  for (const auto& key : keys) {
    const FdWithBoundaries& f = files_.files[FindFile(key)];
    if (user_comparator_->Compare(key, ExtractUserKey(f.smallest.key)) < 0) {
      reader_list.push_back(nullptr);
    } else {
      LookupKey lkey(key, kMaxSequenceNumber);
      f.fd.table_reader->Prepare(lkey.internal_key());
      reader_list.push_back(f.fd.table_reader);
    }
  }
  std::vector<Status> statuses(keys.size(), STATUS(NotFound, ""));
  values->resize(keys.size());
  int idx = 0;
  for (auto* r : reader_list) {
    if (r != nullptr) {
      GetContext get_context(user_comparator_, nullptr, nullptr, nullptr,
                             GetContext::kNotFound, keys[idx], &(*values)[idx],
                             nullptr, nullptr, nullptr);
      LookupKey lkey(keys[idx], kMaxSequenceNumber);
      auto status = r->Get(options, lkey.internal_key(), &get_context);
      if (!status.ok()) {
        statuses[idx] = status;
      } else if (get_context.State() == GetContext::kFound) {
        statuses[idx] = Status::OK();
      }
    }
    ++idx;
  }
  return statuses;
}

Status CompactedDBImpl::Init(const Options& options) {
  {
    std::unique_ptr<SuperVersion> old_superversion;
    InstrumentedMutexLock lock(&mutex_);
    ColumnFamilyDescriptor cf(kDefaultColumnFamilyName,
                              ColumnFamilyOptions(options));
    RETURN_NOT_OK(Recover({ cf }, true /* read only */, false));
    cfd_ = down_cast<ColumnFamilyHandleImpl*>(DefaultColumnFamily())->cfd();
    old_superversion = cfd_->InstallSuperVersion(new SuperVersion(), &mutex_);
  }
  version_ = cfd_->GetSuperVersion()->current;
  user_comparator_ = cfd_->user_comparator();
  auto* vstorage = version_->storage_info();
  if (vstorage->num_non_empty_levels() == 0) {
    return STATUS(NotSupported, "no file exists");
  }
  const LevelFilesBrief& l0 = vstorage->LevelFilesBrief(0);
  // L0 should not have files
  if (l0.num_files > 1) {
    return STATUS(NotSupported, "L0 contain more than 1 file");
  }
  if (l0.num_files == 1) {
    if (vstorage->num_non_empty_levels() > 1) {
      return STATUS(NotSupported, "Both L0 and other level contain files");
    }
    files_ = l0;
    return Status::OK();
  }

  for (int i = 1; i < vstorage->num_non_empty_levels() - 1; ++i) {
    if (vstorage->LevelFilesBrief(i).num_files > 0) {
      return STATUS(NotSupported, "Other levels also contain files");
    }
  }

  int level = vstorage->num_non_empty_levels() - 1;
  if (vstorage->LevelFilesBrief(level).num_files > 0) {
    files_ = vstorage->LevelFilesBrief(level);
    return Status::OK();
  }
  return STATUS(NotSupported, "no file exists");
}

Status CompactedDBImpl::Open(const Options& options,
                             const std::string& dbname, DB** dbptr) {
  *dbptr = nullptr;

  if (options.max_open_files != -1) {
    return STATUS(InvalidArgument, "require max_open_files = -1");
  }
  if (options.merge_operator.get() != nullptr) {
    return STATUS(InvalidArgument, "merge operator is not supported");
  }
  DBOptions db_options(options);
  std::unique_ptr<CompactedDBImpl> db(new CompactedDBImpl(db_options, dbname));
  Status s = db->Init(options);
  if (s.ok()) {
    RLOG(INFO_LEVEL, db->db_options_.info_log,
        "Opened the db as fully compacted mode");
    LogFlush(db->db_options_.info_log);
    *dbptr = db.release();
  }
  return s;
}

}   // namespace rocksdb
