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

#pragma once

#include <string>
#include <vector>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/db.h"

namespace rocksdb {

class Transaction;

// Database with Transaction support.
//
// See optimistic_transaction.h and examples/transaction_example.cc

// Options to use when starting an Optimistic Transaction
struct OptimisticTransactionOptions {
  // Setting set_snapshot=true is the same as calling SetSnapshot().
  bool set_snapshot = false;

  // Should be set if the DB has a non-default comparator.
  // See comment in WriteBatchWithIndex constructor.
  const Comparator* cmp = BytewiseComparator();
};

class OptimisticTransactionDB {
 public:
  // Open an OptimisticTransactionDB similar to DB::Open().
  static Status Open(const Options& options, const std::string& dbname,
                     OptimisticTransactionDB** dbptr);

  static Status Open(const DBOptions& db_options, const std::string& dbname,
                     const std::vector<ColumnFamilyDescriptor>& column_families,
                     std::vector<ColumnFamilyHandle*>* handles,
                     OptimisticTransactionDB** dbptr);

  virtual ~OptimisticTransactionDB() {}

  // Starts a new Transaction.
  //
  // Caller is responsible for deleting the returned transaction when no
  // longer needed.
  //
  // If old_txn is not null, BeginTransaction will reuse this Transaction
  // handle instead of allocating a new one.  This is an optimization to avoid
  // extra allocations when repeatedly creating transactions.
  virtual Transaction* BeginTransaction(
      const WriteOptions& write_options,
      const OptimisticTransactionOptions& txn_options =
          OptimisticTransactionOptions(),
      Transaction* old_txn = nullptr) = 0;

  // Return the underlying Database that was opened
  virtual DB* GetBaseDB() = 0;

 protected:
  // To Create an OptimisticTransactionDB, call Open()
  explicit OptimisticTransactionDB(DB* db) {}
  OptimisticTransactionDB() {}

 private:
  // No copying allowed
  OptimisticTransactionDB(const OptimisticTransactionDB&);
  void operator=(const OptimisticTransactionDB&);
};

}  // namespace rocksdb
