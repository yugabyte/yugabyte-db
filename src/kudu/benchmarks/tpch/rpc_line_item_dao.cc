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

#include <boost/thread/locks.hpp>
#include <glog/logging.h>
#include <vector>
#include <utility>

#include "kudu/benchmarks/tpch/rpc_line_item_dao.h"
#include "kudu/client/callbacks.h"
#include "kudu/client/client.h"
#include "kudu/client/meta_cache.h"
#include "kudu/client/value.h"
#include "kudu/client/write_op.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/util/coding.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

DEFINE_bool(tpch_cache_blocks_when_scanning, true,
            "Whether the scanners should cache the blocks that are read or not");

namespace kudu {

using client::KuduInsert;
using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduError;
using client::KuduPredicate;
using client::KuduRowResult;
using client::KuduScanner;
using client::KuduSchema;
using client::KuduSession;
using client::KuduStatusCallback;
using client::KuduStatusMemberCallback;
using client::KuduTableCreator;
using client::KuduUpdate;
using client::KuduValue;
using std::vector;

namespace {

class FlushCallback : public KuduStatusCallback {
 public:
  FlushCallback(client::sp::shared_ptr<KuduSession> session, Semaphore* sem)
      : session_(std::move(session)),
        sem_(sem) {
    sem_->Acquire();
  }

  virtual void Run(const Status& s) OVERRIDE {
    BatchFinished();
    CHECK_OK(s);
    sem_->Release();
    delete this;
  }

 private:
  void BatchFinished() {
    int nerrs = session_->CountPendingErrors();
    if (nerrs) {
      LOG(WARNING) << nerrs << " errors occured during last batch.";
      vector<KuduError*> errors;
      ElementDeleter d(&errors);
      bool overflow;
      session_->GetPendingErrors(&errors, &overflow);
      if (overflow) {
        LOG(WARNING) << "Error overflow occured";
      }
      for (KuduError* error : errors) {
        LOG(WARNING) << "FAILED: " << error->failed_op().ToString();
      }
    }
  }

  client::sp::shared_ptr<KuduSession> session_;
  Semaphore *sem_;
};

} // anonymous namespace

const Slice RpcLineItemDAO::kScanUpperBound = Slice("1998-09-02");

void RpcLineItemDAO::Init() {
  const KuduSchema schema = tpch::CreateLineItemSchema();

  CHECK_OK(KuduClientBuilder()
           .add_master_server_addr(master_address_)
           .default_rpc_timeout(timeout_)
           .Build(&client_));
  Status s = client_->OpenTable(table_name_, &client_table_);
  if (s.IsNotFound()) {
    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    CHECK_OK(table_creator->table_name(table_name_)
             .schema(&schema)
             .num_replicas(1)
             .split_rows(tablet_splits_)
             .Create());
    CHECK_OK(client_->OpenTable(table_name_, &client_table_));
  } else {
    CHECK_OK(s);
  }

  session_ = client_->NewSession();
  session_->SetTimeoutMillis(timeout_.ToMilliseconds());
  CHECK_OK(session_->SetFlushMode(KuduSession::MANUAL_FLUSH));
}

void RpcLineItemDAO::WriteLine(boost::function<void(KuduPartialRow*)> f) {
  gscoped_ptr<KuduInsert> insert(client_table_->NewInsert());
  f(insert->mutable_row());
  CHECK_OK(session_->Apply(insert.release()));
  ++batch_size_;
  FlushIfBufferFull();
}

void RpcLineItemDAO::FlushIfBufferFull() {
  if (batch_size_ < batch_max_) return;

  batch_size_ = 0;

  // The callback object frees itself after it is invoked.
  session_->FlushAsync(new FlushCallback(session_, &semaphore_));
}

void RpcLineItemDAO::MutateLine(boost::function<void(KuduPartialRow*)> f) {
  gscoped_ptr<KuduUpdate> update(client_table_->NewUpdate());
  f(update->mutable_row());
  CHECK_OK(session_->Apply(update.release()));
  ++batch_size_;
  FlushIfBufferFull();
}

void RpcLineItemDAO::FinishWriting() {
  FlushCallback* cb = new FlushCallback(session_, &semaphore_);
  Status s = session_->Flush();

  // Also deletes 'cb'.
  cb->Run(s);
}

void RpcLineItemDAO::OpenScanner(const vector<string>& columns,
                                 gscoped_ptr<Scanner>* out_scanner) {
  vector<KuduPredicate*> preds;
  OpenScanner(columns, preds, out_scanner);
}

void RpcLineItemDAO::OpenScanner(const vector<string>& columns,
                                 const vector<KuduPredicate*>& preds,
                                 gscoped_ptr<Scanner>* out_scanner) {
  gscoped_ptr<Scanner> ret(new Scanner);
  ret->scanner_.reset(new KuduScanner(client_table_.get()));
  ret->scanner_->SetCacheBlocks(FLAGS_tpch_cache_blocks_when_scanning);
  CHECK_OK(ret->scanner_->SetProjectedColumns(columns));
  for (KuduPredicate* pred : preds) {
    CHECK_OK(ret->scanner_->AddConjunctPredicate(pred));
  }
  CHECK_OK(ret->scanner_->Open());
  out_scanner->swap(ret);
}

void RpcLineItemDAO::OpenTpch1Scanner(gscoped_ptr<Scanner>* out_scanner) {
  vector<KuduPredicate*> preds;
  preds.push_back(client_table_->NewComparisonPredicate(
                      tpch::kShipDateColName, KuduPredicate::LESS_EQUAL,
                      KuduValue::CopyString(kScanUpperBound)));
  OpenScanner(tpch::GetTpchQ1QueryColumns(), preds, out_scanner);
}

void RpcLineItemDAO::OpenTpch1ScannerForOrderKeyRange(int64_t min_key, int64_t max_key,
                                                      gscoped_ptr<Scanner>* out_scanner) {
  vector<KuduPredicate*> preds;
  preds.push_back(client_table_->NewComparisonPredicate(
                      tpch::kShipDateColName, KuduPredicate::LESS_EQUAL,
                      KuduValue::CopyString(kScanUpperBound)));
  preds.push_back(client_table_->NewComparisonPredicate(
                      tpch::kOrderKeyColName, KuduPredicate::GREATER_EQUAL,
                      KuduValue::FromInt(min_key)));
  preds.push_back(client_table_->NewComparisonPredicate(
                      tpch::kOrderKeyColName, KuduPredicate::LESS_EQUAL,
                      KuduValue::FromInt(max_key)));
  OpenScanner(tpch::GetTpchQ1QueryColumns(), preds, out_scanner);
}

bool RpcLineItemDAO::Scanner::HasMore() {
  bool has_more = scanner_->HasMoreRows();
  if (!has_more) {
    scanner_->Close();
  }
  return has_more;
}

void RpcLineItemDAO::Scanner::GetNext(vector<KuduRowResult> *rows) {
  CHECK_OK(scanner_->NextBatch(rows));
}

bool RpcLineItemDAO::IsTableEmpty() {
  KuduScanner scanner(client_table_.get());
  CHECK_OK(scanner.Open());
  return !scanner.HasMoreRows();
}

RpcLineItemDAO::~RpcLineItemDAO() {
  FinishWriting();
}

RpcLineItemDAO::RpcLineItemDAO(string master_address, string table_name,
                               int batch_size, int mstimeout,
                               vector<const KuduPartialRow*> tablet_splits)
    : master_address_(std::move(master_address)),
      table_name_(std::move(table_name)),
      timeout_(MonoDelta::FromMilliseconds(mstimeout)),
      batch_max_(batch_size),
      tablet_splits_(std::move(tablet_splits)),
      batch_size_(0),
      semaphore_(1) {
}

} // namespace kudu
