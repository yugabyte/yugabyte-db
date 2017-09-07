// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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
#ifndef YB_BENCHMARKS_TPCH_RPC_LINE_ITEM_DAO_H
#define YB_BENCHMARKS_TPCH_RPC_LINE_ITEM_DAO_H

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "yb/benchmarks/tpch/tpch-schemas.h"
#include "yb/client/client.h"
#include "yb/client/row_result.h"
#include "yb/util/locks.h"
#include "yb/util/monotime.h"
#include "yb/util/semaphore.h"

namespace yb {

class RpcLineItemDAO {
 public:
  class Scanner;

  RpcLineItemDAO(std::string master_address,
                 client::YBTableName table_name,
                 int batch_size,
                 int mstimeout = 5000,
                 std::vector<const YBPartialRow*> tablet_splits = {});
  ~RpcLineItemDAO();
  void WriteLine(std::function<void(YBPartialRow*)> f);
  void MutateLine(std::function<void(YBPartialRow*)> f);
  void Init();
  void FinishWriting();

  // Deletes previous scanner if one is open.
  // Projects only those column names listed in 'columns'.
  void OpenScanner(const std::vector<std::string>& columns,
                   gscoped_ptr<Scanner>* scanner);
  // Calls OpenScanner with the tpch1 query parameters.
  void OpenTpch1Scanner(gscoped_ptr<Scanner>* scanner);

  // Opens a scanner with the TPCH Q1 projection and filter, plus range filter to only
  // select rows in the given order key range.
  void OpenTpch1ScannerForOrderKeyRange(int64_t min_orderkey, int64_t max_orderkey,
                                        gscoped_ptr<Scanner>* scanner);
  bool IsTableEmpty();

  // TODO: this wrapper class is of limited utility now that we only have a single
  // "DAO" implementation -- we could just return the YBScanner to users directly.
  class Scanner {
   public:
    ~Scanner() {}

    // Return true if there are more rows left in the scanner.
    bool HasMore();

    // Return the next batch of rows into '*rows'. Any existing data is cleared.
    void GetNext(std::vector<client::YBRowResult> *rows);

   private:
    friend class RpcLineItemDAO;
    Scanner() {}

    gscoped_ptr<client::YBScanner> scanner_;
  };

 private:
  static const Slice kScanUpperBound;

  void FlushIfBufferFull();
  void OpenScanner(const std::vector<std::string>& columns,
                   const std::vector<client::YBPredicate*>& preds,
                   gscoped_ptr<Scanner>* scanner);

  simple_spinlock lock_;
  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBSession> session_;
  std::shared_ptr<client::YBTable> client_table_;
  const std::string master_address_;
  const client::YBTableName table_name_;
  const MonoDelta timeout_;
  const int batch_max_;
  const std::vector<const YBPartialRow*> tablet_splits_;
  int batch_size_;

  // Semaphore which restricts us to one batch at a time.
  Semaphore semaphore_;
};

} // namespace yb
#endif // YB_BENCHMARKS_TPCH_RPC_LINE_ITEM_DAO_H
