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


#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema-internal.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/test_workload.h"
#include "yb/util/env.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random.h"
#include "yb/util/thread.h"

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;;
using client::YBInsert;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSchemaFromSchema;
using client::YBSession;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using client::YBUpdate;
using std::shared_ptr;

const YBTableName TestWorkload::kDefaultTableName("my_keyspace", "test-workload");

TestWorkload::TestWorkload(ExternalMiniCluster* cluster)
  : cluster_(cluster),
    payload_bytes_(11),
    num_write_threads_(4),
    write_batch_size_(50),
    write_timeout_millis_(20000),
    timeout_allowed_(false),
    not_found_allowed_(false),
    pathological_one_row_enabled_(false),
    num_replicas_(3),
    num_tablets_(1),
    table_name_(kDefaultTableName),
    start_latch_(0),
    should_run_(false),
    rows_inserted_(0),
    batches_completed_(0) {
}

TestWorkload::~TestWorkload() {
  StopAndJoin();
}

void TestWorkload::WriteThread() {
  Random r(Env::Default()->gettid());

  shared_ptr<YBTable> table;
  // Loop trying to open up the table. In some tests we set up very
  // low RPC timeouts to test those behaviors, so this might fail and
  // need retrying.
  while (should_run_.Load()) {
    Status s = client_->OpenTable(table_name_, &table);
    if (s.ok()) {
      break;
    }
    if (timeout_allowed_ && s.IsTimedOut()) {
      SleepFor(MonoDelta::FromMilliseconds(50));
      continue;
    }
    CHECK_OK(s);
  }

  shared_ptr<YBSession> session = client_->NewSession();
  session->SetTimeoutMillis(write_timeout_millis_);
  CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));

  // Wait for all of the workload threads to be ready to go. This maximizes the chance
  // that they all send a flood of requests at exactly the same time.
  //
  // This also minimizes the chance that we see failures to call OpenTable() if
  // a late-starting thread overlaps with the flood of outbound traffic from the
  // ones that are already writing data.
  start_latch_.CountDown();
  start_latch_.Wait();

  while (should_run_.Load()) {
    for (int i = 0; i < write_batch_size_; i++) {
      if (pathological_one_row_enabled_) {
        shared_ptr<YBUpdate> update(table->NewUpdate());
        YBPartialRow* row = update->mutable_row();
        CHECK_OK(row->SetInt32(0, 0));
        CHECK_OK(row->SetInt32(1, r.Next()));
        CHECK_OK(session->Apply(update));
      } else {
        shared_ptr<YBInsert> insert(table->NewInsert());
        YBPartialRow* row = insert->mutable_row();
        CHECK_OK(row->SetInt32(0, r.Next()));
        CHECK_OK(row->SetInt32(1, r.Next()));
        string test_payload("hello world");
        if (payload_bytes_ != 11) {
          // We fill with zeros if you change the default.
          test_payload.assign(payload_bytes_, '0');
        }
        CHECK_OK(row->SetStringCopy(2, test_payload));
        CHECK_OK(session->Apply(insert));
      }
    }

    int inserted = write_batch_size_;

    Status s = session->Flush();

    if (PREDICT_FALSE(!s.ok())) {
      client::CollectedErrors errors;
      bool overflow;
      session->GetPendingErrors(&errors, &overflow);
      CHECK(!overflow);
      for (const auto& e : errors) {
        if (timeout_allowed_ && e->status().IsTimedOut()) {
          continue;
        }

        if (not_found_allowed_ && e->status().IsNotFound()) {
          continue;
        }
        // We don't handle write idempotency yet. (i.e making sure that when a leader fails
        // writes to it that were eventually committed by the new leader but un-ackd to the
        // client are not retried), so some errors are expected.
        // It's OK as long as the errors are Status::AlreadyPresent();
        CHECK(e->status().IsAlreadyPresent()) << "Unexpected error: " << e->status().ToString();
      }
      inserted -= errors.size();
    }

    rows_inserted_.IncrementBy(inserted);
    if (inserted > 0) {
      batches_completed_.Increment();
    }
  }
}

void TestWorkload::Setup(YBTableType table_type) {
  CHECK_OK(cluster_->CreateClient(&client_builder_, &client_));
  CHECK_OK(client_->CreateNamespaceIfNotExists(table_name_.namespace_name()));

  bool table_exists = false;

  // Retry YBClient::TableExists() until we make that call retry reliably.
  // See KUDU-1074.
  MonoTime deadline(MonoTime::Now(MonoTime::FINE));
  deadline.AddDelta(MonoDelta::FromSeconds(10));
  Status s;
  while (true) {
    s = client_->TableExists(table_name_, &table_exists);
    if (s.ok() || deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) break;
    SleepFor(MonoDelta::FromMilliseconds(10));
  }
  CHECK_OK(s);

  if (!table_exists) {
    YBSchema client_schema(YBSchemaFromSchema(GetSimpleTestSchema()));

    vector<const YBPartialRow*> splits;
    for (int i = 1; i < num_tablets_; i++) {
      YBPartialRow* r = client_schema.NewRow();
      CHECK_OK(r->SetInt32("key", MathLimits<int32_t>::kMax / num_tablets_ * i));
      splits.push_back(r);
    }

    gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    CHECK_OK(table_creator->table_name(table_name_)
             .schema(&client_schema)
             .num_replicas(num_replicas_)
             .split_rows(splits)
             // NOTE: this is quite high as a timeout, but the default (5 sec) does not
             // seem to be high enough in some cases (see KUDU-550). We should remove
             // this once that ticket is addressed.
             .timeout(MonoDelta::FromSeconds(20))
             .table_type(table_type)
             .Create());
  } else {
    LOG(INFO) << "TestWorkload: Skipping table creation because table "
              << table_name_.ToString() << " already exists";
  }


  if (pathological_one_row_enabled_) {
    shared_ptr<YBSession> session = client_->NewSession();
    session->SetTimeoutMillis(20000);
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    shared_ptr<YBTable> table;
    CHECK_OK(client_->OpenTable(table_name_, &table));
    shared_ptr<YBInsert> insert(table->NewInsert());
    YBPartialRow* row = insert->mutable_row();
    CHECK_OK(row->SetInt32(0, 0));
    CHECK_OK(row->SetInt32(1, 0));
    CHECK_OK(row->SetStringCopy(2, "hello world"));
    CHECK_OK(session->Apply(insert));
    CHECK_OK(session->Flush());
  }
}

void TestWorkload::Start() {
  CHECK(!should_run_.Load()) << "Already started";
  should_run_.Store(true);
  start_latch_.Reset(num_write_threads_);
  for (int i = 0; i < num_write_threads_; i++) {
    scoped_refptr<yb::Thread> new_thread;
    CHECK_OK(yb::Thread::Create("test", strings::Substitute("test-writer-$0", i),
                                  &TestWorkload::WriteThread, this,
                                  &new_thread));
    threads_.push_back(new_thread);
  }
}

void TestWorkload::StopAndJoin() {
  should_run_.Store(false);
  start_latch_.Reset(0);
  for (scoped_refptr<yb::Thread> thr : threads_) {
    CHECK_OK(ThreadJoiner(thr.get()).Join());
  }
  threads_.clear();
}

}  // namespace yb
