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


#include <algorithm>
#include <map>
#include <vector>

#include "kudu/client/client-test-util.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/cluster_verifier.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/util/random.h"
#include "kudu/util/random_util.h"
#include "kudu/util/test_util.h"

namespace kudu {

using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduColumnSchema;
using client::KuduError;
using client::KuduInsert;
using client::KuduSchema;
using client::KuduSchemaBuilder;
using client::KuduSession;
using client::KuduTable;
using client::KuduTableAlterer;
using client::KuduTableCreator;
using client::KuduValue;
using client::KuduWriteOperation;
using client::sp::shared_ptr;
using std::make_pair;
using std::map;
using std::pair;
using std::vector;
using strings::SubstituteAndAppend;

const char* kTableName = "test-table";
const int kMaxColumns = 30;

class AlterTableRandomized : public KuduTest {
 public:
  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();

    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    // Because this test performs a lot of alter tables, we end up flushing
    // and rewriting metadata files quite a bit. Globally disabling fsync
    // speeds the test runtime up dramatically.
    opts.extra_tserver_flags.push_back("--never_fsync");
    // This test produces tables with lots of columns. With container preallocation,
    // we end up using quite a bit of disk space. So, we disable it.
    opts.extra_tserver_flags.push_back("--log_container_preallocate_bytes=0");
    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    KuduClientBuilder builder;
    ASSERT_OK(cluster_->CreateClient(builder, &client_));
  }

  virtual void TearDown() OVERRIDE {
    cluster_->Shutdown();
    KuduTest::TearDown();
  }

  void RestartTabletServer(int idx) {
    LOG(INFO) << "Restarting TS " << idx;
    cluster_->tablet_server(idx)->Shutdown();
    CHECK_OK(cluster_->tablet_server(idx)->Restart());
    CHECK_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(idx),
        MonoDelta::FromSeconds(60)));
  }

 protected:
  gscoped_ptr<ExternalMiniCluster> cluster_;
  shared_ptr<KuduClient> client_;
};

struct RowState {
  // We use this special value to denote NULL values.
  // We ensure that we never insert or update to this value except in the case of
  // NULLable columns.
  static const int32_t kNullValue = 0xdeadbeef;
  vector<pair<string, int32_t> > cols;

  string ToString() const {
    string ret = "(";
    typedef pair<string, int32_t> entry;
    bool first = true;
    for (const entry& e : cols) {
      if (!first) {
        ret.append(", ");
      }
      first = false;
      if (e.second == kNullValue) {
        SubstituteAndAppend(&ret, "int32 $0=$1", e.first, "NULL");
      } else {
        SubstituteAndAppend(&ret, "int32 $0=$1", e.first, e.second);
      }
    }
    ret.push_back(')');
    return ret;
  }
};

struct TableState {
  TableState() {
    col_names_.push_back("key");
    col_nullable_.push_back(false);
  }

  ~TableState() {
    STLDeleteValues(&rows_);
  }

  void GenRandomRow(int32_t key, int32_t seed,
                    vector<pair<string, int32_t> >* row) {
    if (seed == RowState::kNullValue) {
      seed++;
    }
    row->clear();
    row->push_back(make_pair("key", key));
    for (int i = 1; i < col_names_.size(); i++) {
      int32_t val;
      if (col_nullable_[i] && seed % 2 == 1) {
        val = RowState::kNullValue;
      } else {
        val = seed;
      }
      row->push_back(make_pair(col_names_[i], val));
    }
  }

  bool Insert(const vector<pair<string, int32_t> >& data) {
    DCHECK_EQ("key", data[0].first);
    int32_t key = data[0].second;
    if (ContainsKey(rows_, key)) return false;

    auto r = new RowState;
    r->cols = data;
    rows_[key] = r;
    return true;
  }

  bool Update(const vector<pair<string, int32_t> >& data) {
    DCHECK_EQ("key", data[0].first);
    int32_t key = data[0].second;
    if (!ContainsKey(rows_, key)) return false;

    RowState* r = rows_[key];
    r->cols = data;
    return true;
  }

  void Delete(int32_t row_key) {
    RowState* r = EraseKeyReturnValuePtr(&rows_, row_key);
    CHECK(r) << "row key " << row_key << " not found";
    delete r;
  }

  void AddColumnWithDefault(const string& name, int32_t def, bool nullable) {
    col_names_.push_back(name);
    col_nullable_.push_back(nullable);
    for (entry& e : rows_) {
      e.second->cols.push_back(make_pair(name, def));
    }
  }

  void DropColumn(const string& name) {
    auto col_it = std::find(col_names_.begin(), col_names_.end(), name);
    int index = col_it - col_names_.begin();
    col_names_.erase(col_it);
    col_nullable_.erase(col_nullable_.begin() + index);
    for (entry& e : rows_) {
      e.second->cols.erase(e.second->cols.begin() + index);
    }
  }

  int32_t GetRandomRowKey(int32_t rand) {
    CHECK(!rows_.empty());
    int idx = rand % rows_.size();
    map<int32_t, RowState*>::const_iterator it = rows_.begin();
    for (int i = 0; i < idx; i++) {
      ++it;
    }
    return it->first;
  }

  void ToStrings(vector<string>* strs) {
    strs->clear();
    for (const entry& e : rows_) {
      strs->push_back(e.second->ToString());
    }
  }

  // The name of each column.
  vector<string> col_names_;

  // For each column, whether it is NULLable.
  // Has the same length as col_names_.
  vector<bool> col_nullable_;

  typedef pair<const int32_t, RowState*> entry;
  map<int32_t, RowState*> rows_;
};

struct MirrorTable {
  explicit MirrorTable(shared_ptr<KuduClient> client)
      : client_(std::move(client)) {}

  Status Create() {
    KuduSchema schema;
    KuduSchemaBuilder b;
    b.AddColumn("key")->Type(KuduColumnSchema::INT32)->NotNull()->PrimaryKey();
    CHECK_OK(b.Build(&schema));
    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    RETURN_NOT_OK(table_creator->table_name(kTableName)
             .schema(&schema)
             .num_replicas(3)
             .Create());
    return Status::OK();
  }

  bool TryInsert(int32_t row_key, int32_t rand) {
    vector<pair<string, int32_t> > row;
    ts_.GenRandomRow(row_key, rand, &row);
    Status s = DoRealOp(row, INSERT);
    if (s.IsAlreadyPresent()) {
      CHECK(!ts_.Insert(row)) << "real table said already-present, fake table succeeded";
      return false;
    }
    CHECK_OK(s);

    CHECK(ts_.Insert(row));
    return true;
  }

  void DeleteRandomRow(uint32_t rand) {
    if (ts_.rows_.empty()) return;
    int32_t row_key = ts_.GetRandomRowKey(rand);
    vector<pair<string, int32_t> > del;
    del.push_back(make_pair("key", row_key));
    CHECK_OK(DoRealOp(del, DELETE));

    ts_.Delete(row_key);
  }

  void UpdateRandomRow(uint32_t rand) {
    if (ts_.rows_.empty()) return;
    int32_t row_key = ts_.GetRandomRowKey(rand);

    vector<pair<string, int32_t> > update;
    update.push_back(make_pair("key", row_key));
    for (int i = 1; i < num_columns(); i++) {
      int32_t val = rand * i;
      if (val == RowState::kNullValue) val++;
      if (ts_.col_nullable_[i] && val % 2 == 1) {
        val = RowState::kNullValue;
      }
      update.push_back(make_pair(ts_.col_names_[i], val));
    }

    if (update.size() == 1) {
      // No columns got updated. Just ignore this update.
      return;
    }

    Status s = DoRealOp(update, UPDATE);
    if (s.IsNotFound()) {
      CHECK(!ts_.Update(update)) << "real table said not-found, fake table succeeded";
      return;
    }
    CHECK_OK(s);

    CHECK(ts_.Update(update));
  }

  void AddAColumn(const string& name) {
    int32_t default_value = rand();
    bool nullable = rand() % 2 == 1;

    // Add to the real table.
    gscoped_ptr<KuduTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));

    if (nullable) {
      default_value = RowState::kNullValue;
      table_alterer->AddColumn(name)->Type(KuduColumnSchema::INT32);
    } else {
      table_alterer->AddColumn(name)->Type(KuduColumnSchema::INT32)->NotNull()
        ->Default(KuduValue::FromInt(default_value));
    }
    ASSERT_OK(table_alterer->Alter());

    // Add to the mirror state.
    ts_.AddColumnWithDefault(name, default_value, nullable);
  }

  void DropAColumn(const string& name) {
    gscoped_ptr<KuduTableAlterer> table_alterer(client_->NewTableAlterer(kTableName));
    CHECK_OK(table_alterer->DropColumn(name)->Alter());
    ts_.DropColumn(name);
  }

  void DropRandomColumn(int seed) {
    if (num_columns() == 1) return;

    string name = ts_.col_names_[1 + (seed % (num_columns() - 1))];
    DropAColumn(name);
  }

  int num_columns() const {
    return ts_.col_names_.size();
  }

  void Verify() {
    // First scan the real table
    vector<string> rows;
    {
      shared_ptr<KuduTable> table;
      CHECK_OK(client_->OpenTable(kTableName, &table));
      client::ScanTableToStrings(table.get(), &rows);
    }
    std::sort(rows.begin(), rows.end());

    // Then get our mock table.
    vector<string> expected;
    ts_.ToStrings(&expected);

    // They should look the same.
    ASSERT_EQ(rows, expected);
  }

 private:
  enum OpType {
    INSERT, UPDATE, DELETE
  };

  Status DoRealOp(const vector<pair<string, int32_t> >& data,
                  OpType op_type) {
    shared_ptr<KuduSession> session = client_->NewSession();
    shared_ptr<KuduTable> table;
    RETURN_NOT_OK(session->SetFlushMode(KuduSession::MANUAL_FLUSH));
    session->SetTimeoutMillis(15 * 1000);
    RETURN_NOT_OK(client_->OpenTable(kTableName, &table));
    gscoped_ptr<KuduWriteOperation> op;
    switch (op_type) {
      case INSERT: op.reset(table->NewInsert()); break;
      case UPDATE: op.reset(table->NewUpdate()); break;
      case DELETE: op.reset(table->NewDelete()); break;
    }
    for (const auto& d : data) {
      if (d.second == RowState::kNullValue) {
        CHECK_OK(op->mutable_row()->SetNull(d.first));
      } else {
        CHECK_OK(op->mutable_row()->SetInt32(d.first, d.second));
      }
    }
    RETURN_NOT_OK(session->Apply(op.release()));
    Status s = session->Flush();
    if (s.ok()) {
      return s;
    }

    std::vector<KuduError*> errors;
    ElementDeleter d(&errors);
    bool overflow;
    session->GetPendingErrors(&errors, &overflow);
    CHECK_EQ(errors.size(), 1);
    return errors[0]->status();
  }

  shared_ptr<KuduClient> client_;
  TableState ts_;
};

// Stress test for various alter table scenarios. This performs a random sequence of:
//   - insert a row (using the latest schema)
//   - delete a random row
//   - update a row (all columns with the latest schema)
//   - add a new column
//   - drop a column
//   - restart the tablet server
//
// During the sequence of operations, a "mirror" of the table in memory is kept up to
// date. We periodically scan the actual table, and ensure that the data in Kudu
// matches our in-memory "mirror".
TEST_F(AlterTableRandomized, TestRandomSequence) {
  MirrorTable t(client_);
  ASSERT_OK(t.Create());

  Random rng(SeedRandom());

  const int n_iters = AllowSlowTests() ? 2000 : 1000;
  for (int i = 0; i < n_iters; i++) {
    // Perform different operations with varying probability.
    // We mostly insert and update, with occasional deletes,
    // and more occasional table alterations or restarts.
    int r = rng.Uniform(1000);
    if (r < 400) {
      t.TryInsert(1000000 + rng.Uniform(1000000), rng.Next());
    } else if (r < 600) {
      t.UpdateRandomRow(rng.Next());
    } else if (r < 920) {
      t.DeleteRandomRow(rng.Next());
    } else if (r < 970) {
      if (t.num_columns() < kMaxColumns) {
        t.AddAColumn(strings::Substitute("c$0", i));
      }
    } else if (r < 995) {
      t.DropRandomColumn(rng.Next());
    } else {
      RestartTabletServer(rng.Uniform(cluster_->num_tablet_servers()));
    }

    if (i % 1000 == 0) {
      NO_FATALS(t.Verify());
    }
  }

  NO_FATALS(t.Verify());

  // Not only should the data returned by a scanner match what we expect,
  // we also expect all of the replicas to agree with each other.
  ClusterVerifier v(cluster_.get());
  NO_FATALS(v.CheckCluster());
}

} // namespace kudu
