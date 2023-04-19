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
#include <atomic>
#include <cassert>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/transaction_log.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/mutable_cf_options.h"

namespace rocksdb {

class WalManager {
 public:
  WalManager(const DBOptions& db_options, const EnvOptions& env_options)
      : db_options_(db_options),
        env_options_(env_options),
        env_(db_options.env),
        purge_wal_files_last_run_(0) {}

  Status GetSortedWalFiles(VectorLogPtr* files);

  Status GetUpdatesSince(
      SequenceNumber seq_number, std::unique_ptr<TransactionLogIterator>* iter,
      const TransactionLogIterator::ReadOptions& read_options,
      VersionSet* version_set);

  void PurgeObsoleteWALFiles();

  void ArchiveWALFile(const std::string& fname, uint64_t number);

  Status TEST_ReadFirstRecord(const WalFileType type, const uint64_t number,
                              SequenceNumber* sequence) {
    return ReadFirstRecord(type, number, sequence);
  }

  Status TEST_ReadFirstLine(const std::string& fname,
                            SequenceNumber* sequence) {
    return ReadFirstLine(fname, sequence);
  }

 private:
  Status GetSortedWalsOfType(const std::string& path, VectorLogPtr* log_files, WalFileType type);
  // Requires: all_logs should be sorted with earliest log file first
  // Retains all log files in all_logs which contain updates with seq no.
  // Greater Than or Equal to the requested SequenceNumber.
  Status RetainProbableWalFiles(VectorLogPtr* all_logs, const SequenceNumber target);

  Status ReadFirstRecord(const WalFileType type, const uint64_t number,
                         SequenceNumber* sequence);

  Status ReadFirstLine(const std::string& fname, SequenceNumber* sequence);

  // ------- state from DBImpl ------
  const DBOptions& db_options_;
  const EnvOptions& env_options_;
  Env* env_;

  // ------- WalManager state -------
  // cache for ReadFirstRecord() calls
  std::unordered_map<uint64_t, SequenceNumber> read_first_record_cache_;
  port::Mutex read_first_record_cache_mutex_;

  // last time when PurgeObsoleteWALFiles ran.
  uint64_t purge_wal_files_last_run_;

  // obsolete files will be deleted every this seconds if ttl deletion is
  // enabled and archive size_limit is disabled.
  static const uint64_t kDefaultIntervalToDeleteObsoleteWAL = 600;
};

}  // namespace rocksdb
