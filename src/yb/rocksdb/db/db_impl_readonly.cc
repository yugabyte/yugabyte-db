//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/rocksdb/db/db_impl_readonly.h"

#include "yb/rocksdb/db/compacted_db_impl.h"
#include "yb/rocksdb/db/db_iter.h"
#include "yb/rocksdb/db/merge_context.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/util/perf_context_imp.h"

#include "yb/util/stats/perf_step_timer.h"

namespace rocksdb {


DBImplReadOnly::DBImplReadOnly(const DBOptions& db_options,
                               const std::string& dbname)
    : DBImpl(db_options, dbname) {
  RLOG(INFO_LEVEL, db_options_.info_log, "Opening the db in read only mode");
  LogFlush(db_options_.info_log);
}

DBImplReadOnly::~DBImplReadOnly() {
}

// Implementations of the DB interface
Status DBImplReadOnly::Get(const ReadOptions& read_options,
                           ColumnFamilyHandle* column_family, const Slice& key,
                           std::string* value) {
  Status s;
  SequenceNumber snapshot = versions_->LastSequence();
  auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
  auto cfd = cfh->cfd();
  SuperVersion* super_version = cfd->GetSuperVersion();
  MergeContext merge_context;
  LookupKey lkey(key, snapshot);
  if (super_version->mem->Get(lkey, value, &s, &merge_context)) {
  } else {
    PERF_TIMER_GUARD(get_from_output_files_time);
    super_version->current->Get(read_options, lkey, value, &s, &merge_context);
  }
  return s;
}

Iterator* DBImplReadOnly::NewIterator(const ReadOptions& read_options,
                                      ColumnFamilyHandle* column_family) {
  auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
  auto cfd = cfh->cfd();
  SuperVersion* super_version = cfd->GetSuperVersion()->Ref();
  SequenceNumber latest_snapshot = versions_->LastSequence();
  auto db_iter = NewArenaWrappedDbIterator(
      env_, *cfd->ioptions(), cfd->user_comparator(),
      (read_options.snapshot != nullptr
           ? reinterpret_cast<const SnapshotImpl*>(read_options.snapshot)
                 ->number_
           : latest_snapshot),
      super_version->mutable_cf_options.max_sequential_skip_in_iterations,
      super_version->version_number);
  auto internal_iter = NewInternalIterator(
      read_options, cfd, super_version, db_iter->GetArena());
  db_iter->SetIterUnderDBIter(internal_iter);
  return db_iter;
}

Status DBImplReadOnly::NewIterators(
    const ReadOptions& read_options,
    const std::vector<ColumnFamilyHandle*>& column_families,
    std::vector<Iterator*>* iterators) {
  if (iterators == nullptr) {
    return STATUS(InvalidArgument, "iterators not allowed to be nullptr");
  }
  iterators->clear();
  iterators->reserve(column_families.size());
  SequenceNumber latest_snapshot = versions_->LastSequence();

  for (auto cfh : column_families) {
    auto* cfd = reinterpret_cast<ColumnFamilyHandleImpl*>(cfh)->cfd();
    auto* sv = cfd->GetSuperVersion()->Ref();
    auto* db_iter = NewArenaWrappedDbIterator(
        env_, *cfd->ioptions(), cfd->user_comparator(),
        (read_options.snapshot != nullptr
             ? reinterpret_cast<const SnapshotImpl*>(read_options.snapshot)
                   ->number_
             : latest_snapshot),
        sv->mutable_cf_options.max_sequential_skip_in_iterations,
        sv->version_number);
    auto* internal_iter = NewInternalIterator(
        read_options, cfd, sv, db_iter->GetArena());
    db_iter->SetIterUnderDBIter(internal_iter);
    iterators->push_back(db_iter);
  }

  return Status::OK();
}

Status DB::OpenForReadOnly(const Options& options, const std::string& dbname,
                           DB** dbptr, bool error_if_log_file_exist) {
  *dbptr = nullptr;

  // Try to first open DB as fully compacted DB
  Status s;
  s = CompactedDBImpl::Open(options, dbname, dbptr);
  if (s.ok()) {
    return s;
  }

  DBOptions db_options(options);
  ColumnFamilyOptions cf_options(options);
  std::vector<ColumnFamilyDescriptor> column_families;
  column_families.push_back(
      ColumnFamilyDescriptor(kDefaultColumnFamilyName, cf_options));
  std::vector<ColumnFamilyHandle*> handles;

  s = DB::OpenForReadOnly(db_options, dbname, column_families, &handles, dbptr);
  if (s.ok()) {
    assert(handles.size() == 1);
    // i can delete the handle since DBImpl is always holding a
    // reference to default column family
    delete handles[0];
  }
  return s;
}

Status DB::OpenForReadOnly(
    const DBOptions& db_options, const std::string& dbname,
    const std::vector<ColumnFamilyDescriptor>& column_families,
    std::vector<ColumnFamilyHandle*>* handles, DB** dbptr,
    bool error_if_log_file_exist) {
  *dbptr = nullptr;
  handles->clear();

  Status s;
  DBImplReadOnly* impl = new DBImplReadOnly(db_options, dbname);
  {
    // Used to destroy superversions outside of lock.
    std::vector<std::unique_ptr<SuperVersion>> old_superversions;

    InstrumentedMutexLock lock(&impl->mutex_);
    s = impl->Recover(column_families, true /* read only */, error_if_log_file_exist);
    if (s.ok()) {
      // set column family handles
      for (auto cf : column_families) {
        auto cfd =
            impl->versions_->GetColumnFamilySet()->GetColumnFamily(cf.name);
        if (cfd == nullptr) {
          s = STATUS(InvalidArgument, "Column family not found: ", cf.name);
          break;
        }
        handles->push_back(new ColumnFamilyHandleImpl(cfd, impl, &impl->mutex_));
      }
    }
    if (s.ok()) {
      for (auto cfd : *impl->versions_->GetColumnFamilySet()) {
        old_superversions.push_back(cfd->InstallSuperVersion(new SuperVersion(), &impl->mutex_));
      }
    }
  }
  if (s.ok()) {
    *dbptr = impl;
  } else {
    for (auto h : *handles) {
      delete h;
    }
    handles->clear();
    delete impl;
  }
  return s;
}


}   // namespace rocksdb
