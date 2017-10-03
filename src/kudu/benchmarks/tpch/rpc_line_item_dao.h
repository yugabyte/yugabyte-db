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
#ifndef KUDU_TPCH_RPC_LINE_ITEM_DAO_H
#define KUDU_TPCH_RPC_LINE_ITEM_DAO_H

#include <boost/function.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "kudu/benchmarks/tpch/tpch-schemas.h"
#include "kudu/client/client.h"
#include "kudu/client/row_result.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/semaphore.h"

namespace kudu {

class RpcLineItemDAO {
 public:
  class Scanner;

  RpcLineItemDAO(std::string master_address,
                 std::string table_name,
                 int batch_size,
                 int mstimeout = 5000,
                 std::vector<const KuduPartialRow*> tablet_splits = {});
  ~RpcLineItemDAO();
  void WriteLine(boost::function<void(KuduPartialRow*)> f);
  void MutateLine(boost::function<void(KuduPartialRow*)> f);
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
  // "DAO" implementation -- we could just return the KuduScanner to users directly.
  class Scanner {
   public:
    ~Scanner() {}

    // Return true if there are more rows left in the scanner.
    bool HasMore();

    // Return the next batch of rows into '*rows'. Any existing data is cleared.
    void GetNext(std::vector<client::KuduRowResult> *rows);

   private:
    friend class RpcLineItemDAO;
    Scanner() {}

    gscoped_ptr<client::KuduScanner> scanner_;
  };

 private:
  static const Slice kScanUpperBound;

  void FlushIfBufferFull();
  void OpenScanner(const std::vector<std::string>& columns,
                   const std::vector<client::KuduPredicate*>& preds,
                   gscoped_ptr<Scanner>* scanner);

  simple_spinlock lock_;
  client::sp::shared_ptr<client::KuduClient> client_;
  client::sp::shared_ptr<client::KuduSession> session_;
  client::sp::shared_ptr<client::KuduTable> client_table_;
  const std::string master_address_;
  const std::string table_name_;
  const MonoDelta timeout_;
  const int batch_max_;
  const std::vector<const KuduPartialRow*> tablet_splits_;
  int batch_size_;

  // Semaphore which restricts us to one batch at a time.
  Semaphore semaphore_;
};

} //namespace kudu
#endif
