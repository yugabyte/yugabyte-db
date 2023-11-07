// Copyright (c) YugaByte, Inc.
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

#include <tuple>

#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strip.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/backfill-test-util.h"
#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"

#include "yb/master/master_admin.proxy.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/jsonreader.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

using std::string;
using std::vector;
using std::ostringstream;
using std::unique_ptr;
using std::tuple;
using std::get;
using std::map;

using rapidjson::Value;
using strings::Substitute;

using yb::CoarseBackoffWaiter;
using yb::YQLDatabase;
using yb::client::TableHandle;
using yb::client::TransactionManager;
using yb::client::YBTableName;
using yb::client::YBTableInfo;
using yb::client::YBqlWriteOpPtr;
using yb::client::YBSessionPtr;

METRIC_DECLARE_entity(server);
METRIC_DECLARE_histogram(handler_latency_yb_client_write_remote);
METRIC_DECLARE_histogram(handler_latency_yb_client_read_remote);
METRIC_DECLARE_histogram(handler_latency_yb_client_write_local);
METRIC_DECLARE_histogram(handler_latency_yb_client_read_local);

METRIC_DECLARE_histogram(handler_latency_yb_cqlserver_SQLProcessor_InsertStmt);
METRIC_DECLARE_histogram(handler_latency_yb_cqlserver_SQLProcessor_UseStmt);

DECLARE_int64(external_mini_cluster_max_log_bytes);
DECLARE_int32(TEST_slowdown_backfill_job_deletion_ms);
DECLARE_int32(index_backfill_tablet_split_completion_timeout_sec);

namespace yb {

namespace util {

template<class T>
string type_name() { return "unknown"; } // COMPILATION ERROR: Specialize it for your type!
// Supported types - get type name:
template<> string type_name<string>() { return "text"; }
template<> string type_name<cass_bool_t>() { return "boolean"; }
template<> string type_name<cass_float_t>() { return "float"; }
template<> string type_name<cass_double_t>() { return "double"; }
template<> string type_name<cass_int32_t>() { return "int"; }
template<> string type_name<cass_int64_t>() { return "bigint"; }
template<> string type_name<CassandraJson>() { return "jsonb"; }

} // namespace util

//------------------------------------------------------------------------------

class CppCassandraDriverTest : public ExternalMiniClusterITestBase {
 public:
  void SetUp() override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUp());

    LOG(INFO) << "Starting YB ExternalMiniCluster...";
    // Start up with 3 (default) tablet servers.
    ASSERT_NO_FATALS(StartCluster(ExtraTServerFlags(), ExtraMasterFlags(), 3, NumMasters()));

    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }
    driver_.reset(new CppCassandraDriver(
        hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kTrue));

    // Create and use default keyspace.
    auto deadline = CoarseMonoClock::now() + 15s;
    while (CoarseMonoClock::now() < deadline) {
      auto session = EstablishSession();
      if (session.ok()) {
        session_ = std::move(*session);
        break;
      }
    }

    // Set up a ClusterAdminClient
    admin_client_.reset(
        new tools::ClusterAdminClient(cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(15)));
    ASSERT_OK(admin_client_->Init());
  }

  void SetUpCluster(ExternalMiniClusterOptions* opts) override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUpCluster(opts));

    opts->bind_to_unique_loopback_addresses = true;
    opts->use_same_ts_ports = true;
  }

  void TearDown() override {
    ExternalMiniClusterITestBase::cluster_->AssertNoCrashes();

    // Close the session before we delete the driver.
    session_.Reset();
    driver_.reset();
    LOG(INFO) << "Stopping YB ExternalMiniCluster...";
    ExternalMiniClusterITestBase::TearDown();
  }

  virtual std::vector<std::string> ExtraTServerFlags() {
    return {};
  }

  virtual std::vector<std::string> ExtraMasterFlags() {
    return {};
  }

  virtual int NumMasters() {
    return 1;
  }

 protected:
  Result<CassandraSession> EstablishSession() {
    auto session = VERIFY_RESULT(driver_->CreateSession());
    RETURN_NOT_OK(SetupSession(&session));
    return session;
  }

  Status CreateDefaultKeyspace(CassandraSession* session) {
    if (!keyspace_created_.load(std::memory_order_acquire)) {
      RETURN_NOT_OK(session->ExecuteQuery("CREATE KEYSPACE IF NOT EXISTS test"));
      keyspace_created_.store(true, std::memory_order_release);
    }
    return Status::OK();
  }

  virtual Status SetupSession(CassandraSession* session) {
    RETURN_NOT_OK(CreateDefaultKeyspace(session));
    return session->ExecuteQuery("USE test");
  }

  unique_ptr<CppCassandraDriver> driver_;
  CassandraSession session_;
  unique_ptr<tools::ClusterAdminClient> admin_client_;
  std::atomic<bool> keyspace_created_{false};
};

YB_STRONGLY_TYPED_BOOL(PKOnlyIndex);
YB_STRONGLY_TYPED_BOOL(IsUnique);
YB_STRONGLY_TYPED_BOOL(IncludeAllColumns);
YB_STRONGLY_TYPED_BOOL(UserEnforced);

class CppCassandraDriverTestIndex : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {
        "--allow_index_table_read_write=true",
        Format("--client_read_write_timeout_ms=$0", 10000 * kTimeMultiplier),
        "--index_backfill_upperbound_for_user_enforced_txn_duration_ms=12000",
        "--yb_client_admin_operation_timeout_sec=90",
    };
  }

  std::vector<std::string> ExtraMasterFlags() override {
    return {
        "--TEST_slowdown_backfill_alter_table_rpcs_ms=200",
        "--TEST_slowdown_backfill_job_deletion_ms=1000",
        "--disable_index_backfill=false",
        "--enable_load_balancing=false",
        "--enable_automatic_tablet_splitting=false",
        "--db_block_size_bytes=2000",
        "--db_filter_block_size_bytes=2000",
        "--db_index_block_size_bytes=2000",
        "--index_backfill_rpc_max_delay_ms=1000",
        "--index_backfill_rpc_max_retries=10",
        "--index_backfill_rpc_timeout_ms=6000",
        "--retrying_ts_rpc_max_delay_ms=1000",
        "--unresponsive_ts_rpc_retry_limit=10",
    };
  }

 protected:
  friend Result<IndexPermissions> TestBackfillCreateIndexTableSimple(
      CppCassandraDriverTestIndex* test, bool deferred, IndexPermissions target_permission);

  friend void TestBackfillIndexTable(CppCassandraDriverTestIndex* test,
                                     PKOnlyIndex is_pk_only, IsUnique is_unique,
                                     IncludeAllColumns include_primary_key,
                                     UserEnforced user_enforced);

  friend void DoTestCreateUniqueIndexWithOnlineWrites(
      CppCassandraDriverTestIndex* test, bool delete_before_insert);

  void TestUniqueIndexCommitOrder(bool commit_txn1, bool use_txn2);
};

class CppCassandraDriverTestIndexSlow : public CppCassandraDriverTestIndex {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraTServerFlags();
    flags.push_back("--TEST_slowdown_backfill_by_ms=150");
    flags.push_back("--num_concurrent_backfills_allowed=1");
    return flags;
  }

  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraMasterFlags();
    flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=200");
    return flags;
  }
};

class CppCassandraDriverTestIndexSlower : public CppCassandraDriverTestIndex {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraTServerFlags();
    flags.push_back("--TEST_slowdown_backfill_by_ms=3000");
    flags.push_back("--ycql_num_tablets=1");
    flags.push_back("--ysql_num_tablets=1");
    flags.push_back("--raft_heartbeat_interval_ms=200");
    return flags;
  }

  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraMasterFlags();
    flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=3000");
    flags.push_back("--vmodule=backfill_index=3");
    return flags;
  }
};

class CppCassandraDriverTestIndexMultipleChunks : public CppCassandraDriverTestIndexSlow {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndexSlow::ExtraTServerFlags();
    flags.push_back("--TEST_backfill_paging_size=2");
    return flags;
  }
};

class CppCassandraDriverTestIndexMultipleChunksWithLeaderMoves
    : public CppCassandraDriverTestIndexMultipleChunks {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraMasterFlags();
    flags.push_back("--enable_load_balancing=true");
    flags.push_back("--index_backfill_rpc_max_retries=0");
    // We do not want backfill to fail because of any throttling.
    flags.push_back("--index_backfill_rpc_timeout_ms=180000");
    return flags;
  }

  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraTServerFlags();
    flags.push_back("--backfill_index_rate_rows_per_sec=10");
    flags.push_back("--backfill_index_write_batch_size=2");
    return flags;
  }

  void SetUp() override {
    CppCassandraDriverTestIndex::SetUp();
    thread_holder_.AddThreadFunctor([this] {
      const auto kNumTServers = cluster_->num_tablet_servers();
      constexpr auto kSleepTimeMs = 5000;
      for (int i = 0; !thread_holder_.stop_flag(); i++) {
        const auto tserver_id = i % kNumTServers;
        ASSERT_OK(cluster_->AddTServerToLeaderBlacklist(
            cluster_->master(), cluster_->tablet_server(tserver_id)));
        SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
        ASSERT_OK(cluster_->ClearBlacklist(cluster_->master()));
      }
    });
  }

  void TearDown() override {
    thread_holder_.Stop();
    CppCassandraDriverTestIndex::TearDown();
  }

 private:
  TestThreadHolder thread_holder_;
};

class CppCassandraDriverTestIndexSlowBackfill : public CppCassandraDriverTestIndex {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraMasterFlags();
    // We expect backfill to be slow, so give it more time.
    flags.push_back("--index_backfill_rpc_max_retries=100");
    return flags;
  }

  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndex::ExtraTServerFlags();
    flags.push_back(Format("--backfill_index_rate_rows_per_sec=$0", kMaxBackfillRatePerSec));
    flags.push_back("--backfill_index_write_batch_size=1");
    flags.push_back("--num_concurrent_backfills_allowed=1");
    return flags;
  }

 protected:
  const size_t kMaxBackfillRatePerSec = 10;
};

class CppCassandraDriverTestUserEnforcedIndex : public CppCassandraDriverTestIndexSlow {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestIndexSlow::ExtraMasterFlags();
    flags.push_back("--disable_index_backfill_for_non_txn_tables=false");
    return flags;
  }

  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTestIndexSlow::ExtraTServerFlags();
    flags.push_back(Format("--client_read_write_timeout_ms=$0", 10000 * kTimeMultiplier));
    flags.push_back(
        "--index_backfill_upperbound_for_user_enforced_txn_duration_ms=12000");
    return flags;
  }
};

class CppCassandraDriverTestIndexNonResponsiveTServers : public CppCassandraDriverTestIndexSlow {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    return {
        "--disable_index_backfill=false",
        "--enable_load_balancing=false",
        "--ycql_num_tablets=18",
        "--ysql_num_tablets=18",
        // Really aggressive timeouts.
        "--index_backfill_rpc_max_retries=1",
        "--index_backfill_rpc_timeout_ms=1",
        "--index_backfill_rpc_max_delay_ms=1"};
  }
};

//------------------------------------------------------------------------------

class Metrics {
 public:
  Metrics(const Metrics&) = default;

  explicit Metrics(const ExternalMiniCluster& cluster, bool cql_metrics)
      : cluster_(cluster), cql_metrics_(cql_metrics) {}

  void reset() {
    for (auto& elem : values_) {
      elem.second = 0;
    }
  }

  void load() {
    reset();
    for (size_t i = 0; i < cluster_.num_tablet_servers(); ++i) {
      for (auto& proto : prototypes_) {
        int64_t metric = 0;
        load_value(cluster_, cql_metrics_, i, proto.second, &metric);
        values_[proto.first] += metric;
      }
    }
  }

  int64_t get(const string& name) const {
    auto it = values_.find(name);
    DCHECK(it != values_.end());
    return it->second;
  }

  Metrics& operator +=(const Metrics& m) {
    for (auto& elem : values_) {
      auto it = m.values_.find(elem.first);
      DCHECK(it != m.values_.end());
      elem.second += it->second;
    }
    return *this;
  }

  Metrics operator -() const {
    Metrics m(*this);
    for (auto& elem : m.values_) {
      elem.second = -elem.second;
    }
    return m;
  }

  Metrics& operator -=(const Metrics& m) {
    return *this += -m;
  }

  Metrics operator +(const Metrics& m) const {
    return Metrics(*this) += m;
  }

  Metrics operator -(const Metrics& m) const {
    return Metrics(*this) += -m;
  }

  string ToString() const {
    string s;
    for (auto& elem : values_) {
      s += (s.empty() ? "" : ", ") + Format("$0=$1", elem.first, elem.second);
    }
    return s;
  }

  static void load_value(
      const ExternalMiniCluster& cluster, bool cql_metric, size_t ts_index,
      const MetricPrototype* metric_proto, int64_t* value) {
    const ExternalTabletServer& ts = *CHECK_NOTNULL(cluster.tablet_server(ts_index));
    const HostPort host_port = cql_metric ?
        HostPort(ts.bind_host(), ts.cql_http_port()) : ts.bound_http_hostport();
    const char* entity_id = cql_metric ? "yb.cqlserver" : "yb.tabletserver";
    const auto result = ts.GetMetricFromHost<int64>(
        host_port, &METRIC_ENTITY_server, entity_id, CHECK_NOTNULL(metric_proto), "total_count");

    if (!result.ok()) {
      LOG(ERROR) << "Failed to get metric " << metric_proto->name() << " from TS"
          << ts_index << ": " << host_port << " with error " << result.status();
    }
    ASSERT_OK(result);
    *CHECK_NOTNULL(value) = *result;
  }

 protected:
  void add_proto(const string& name, const MetricPrototype* proto) {
    prototypes_[name] = proto;
    values_[name] = 0;
  }

 private:
  const ExternalMiniCluster& cluster_;
  const bool cql_metrics_;

  map<string, const MetricPrototype*> prototypes_;
  map<string, int64_t> values_;
};

std::ostream& operator <<(std::ostream& s, const Metrics& m) {
  return s << m.ToString();
}

struct IOMetrics : public Metrics {
  explicit IOMetrics(const Metrics& m) : Metrics(m) {}

  explicit IOMetrics(const ExternalMiniCluster& cluster) : Metrics(cluster, false) {
    add_proto("remote_write", &METRIC_handler_latency_yb_client_write_remote);
    add_proto("remote_read", &METRIC_handler_latency_yb_client_read_remote);
    add_proto("local_write", &METRIC_handler_latency_yb_client_write_local);
    add_proto("local_read", &METRIC_handler_latency_yb_client_read_local);
    load();
  }
};

struct CQLMetrics : public Metrics {
  explicit CQLMetrics(const Metrics& m) : Metrics(m) {}

  explicit CQLMetrics(const ExternalMiniCluster& cluster) : Metrics(cluster, true) {
    add_proto("insert_count", &METRIC_handler_latency_yb_cqlserver_SQLProcessor_InsertStmt);
    add_proto("use_count", &METRIC_handler_latency_yb_cqlserver_SQLProcessor_UseStmt);
    load();
  }
};

//------------------------------------------------------------------------------

template <typename... ColumnsTypes>
class TestTable {
 public:
  typedef vector<string> StringVec;
  typedef tuple<ColumnsTypes...> ColumnsTuple;

  Status CreateTable(
      CassandraSession* session, const string& table, const StringVec& columns,
      const StringVec& keys, bool transactional = false,
      const MonoDelta& timeout = 60s) {
    table_name_ = table;
    column_names_ = columns;
    key_names_ = keys;

    for (string& k : key_names_) {
      TrimString(&k, "()"); // Cut parentheses if available.
    }

    auto deadline = CoarseMonoClock::now() + timeout;
    CoarseBackoffWaiter waiter(deadline, 2500ms * kTimeMultiplier);
    for (;;) {
      const std::string query = create_table_str(table, columns, keys, transactional);
      auto result = session->ExecuteQuery(query);
      if (result.ok() || CoarseMonoClock::now() >= deadline) {
        return result;
      }
      WARN_NOT_OK(result, "Create table failed");
      waiter.Wait();
    }
  }

  void Print(const string& prefix, const ColumnsTuple& data) const {
    LOG(INFO) << prefix << ":";

    StringVec types;
    do_get_type_names(&types, data);
    CHECK_EQ(types.size(), column_names_.size());

    StringVec values;
    do_get_values(&values, data);
    CHECK_EQ(values.size(), column_names_.size());

    for (size_t i = 0; i < column_names_.size(); ++i) {
      LOG(INFO) << ">     " << column_names_[i] << ' ' << types[i] << ": " << values[i];
    }
  }

  void BindInsert(CassandraStatement* statement, const ColumnsTuple& data) const {
    DoBindValues(statement, /* keys_only = */ false, /* values_first = */ false, data);
  }

  void Insert(CassandraSession* session, const ColumnsTuple& data) const {
    const string query = insert_with_bindings_str(table_name_, column_names_);
    Print("Execute: '" + query + "' with data", data);

    CassandraStatement statement(cass_statement_new(query.c_str(), column_names_.size()));
    BindInsert(&statement, data);
    ASSERT_OK(session->Execute(statement));
  }

  Result<CassandraPrepared> PrepareInsert(CassandraSession* session,
                                          MonoDelta timeout = MonoDelta::kZero,
                                          const string& local_keyspace = string()) const {
    return session->Prepare(
        insert_with_bindings_str(table_name_, column_names_), timeout, local_keyspace);
  }

  void Update(CassandraSession* session, const ColumnsTuple& data) const {
    const string query = update_with_bindings_str(table_name_, column_names_, key_names_);
    Print("Execute: '" + query + "' with data", data);

    CassandraStatement statement(cass_statement_new(query.c_str(), column_names_.size()));
    DoBindValues(&statement, /* keys_only = */ false, /* values_first = */ true, data);

    ASSERT_OK(session->Execute(statement));
  }

  void SelectOneRow(CassandraSession* session, ColumnsTuple* data) {
    const string query = select_with_bindings_str(table_name_, key_names_);
    Print("Execute: '" + query + "' with data", *data);

    CassandraStatement statement(cass_statement_new(query.c_str(), key_names_.size()));
    DoBindValues(&statement, /* keys_only = */ true, /* values_first = */ false, *data);
    *data = ASSERT_RESULT(ExecuteAndReadOneRow(session, statement));
  }

  Result<ColumnsTuple> SelectByToken(CassandraSession* session, int64_t token) {
    const string query = select_by_token_str(table_name_, key_names_);
    LOG(INFO) << "Execute: '" << query << "' with token: " << token;

    CassandraStatement statement(query, 1);
    statement.Bind(0, token);
    return ExecuteAndReadOneRow(session, statement);
  }

  Result<ColumnsTuple> ExecuteAndReadOneRow(
      CassandraSession* session, const CassandraStatement& statement) {
    ColumnsTuple data;
    RETURN_NOT_OK(session->ExecuteAndProcessOneRow(
        statement, [this, &data](const CassandraRow& row) {
          DoReadValues(row, &data);
        }));
    return data;
  }

 protected:
  // Tuple unrolling methods.
  static void get_type_names(StringVec*, const tuple<>&) {}

  template<typename T, typename... A>
  static void get_type_names(StringVec* types, const tuple<T, A...>& t) {
    types->push_back(util::type_name<T>());
    get_type_names(types, tuple<A...>());
  }

  static void do_get_type_names(StringVec* types, const ColumnsTuple& t) {
    get_type_names<ColumnsTypes...>(types, t);
  }

  template<size_t I>
  static void get_values(StringVec*, const ColumnsTuple&) {}

  template<size_t I, typename T, typename... A>
  static void get_values(StringVec* values, const ColumnsTuple& data) {
    ostringstream ss;
    ss << get<I>(data);
    values->push_back(ss.str());
    get_values<I + 1, A...>(values, data);
  }

  static void do_get_values(StringVec* values, const ColumnsTuple& data) {
    get_values<0, ColumnsTypes...>(values, data);
  }

  template<size_t I>
  typename std::enable_if<I >= std::tuple_size<ColumnsTuple>::value, void>::type
  BindValues(CassandraStatement*, size_t*, bool, bool, const ColumnsTuple&) const {}

  template<size_t I>
  typename std::enable_if<I < std::tuple_size<ColumnsTuple>::value, void>::type
  BindValues(
      CassandraStatement* statement, size_t* index, bool use_values, bool use_keys,
      const ColumnsTuple& data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if (this_is_key ? use_keys : use_values) {
      statement->Bind((*index)++, get<I>(data));
    }

    BindValues<I + 1>(statement, index, use_values, use_keys, data);
  }

  void DoBindValues(
      CassandraStatement* statement, bool keys_only, bool values_first,
      const ColumnsTuple& data) const {
    size_t i = 0;
    if (keys_only) {
      BindValues<0>(statement, &i, false /* use_values */, true /* use_keys */, data);
    } else if (values_first) {
      // Bind values.
      BindValues<0>(statement, &i, true /* use_values */, false /* use_keys */, data);
      // Bind keys.
      BindValues<0>(statement, &i, false /* use_values */, true /* use_keys */, data);
    } else {
      BindValues<0>(statement, &i, true /* use_values */, true /* use_keys */, data);
    }
  }

  template<size_t I>
  typename std::enable_if<I >= std::tuple_size<ColumnsTuple>::value, void>::type
  ReadValues(const CassandraRow& row, size_t*, bool, ColumnsTuple*) const {}

  template<size_t I>
  typename std::enable_if<I < std::tuple_size<ColumnsTuple>::value, void>::type
  ReadValues(
      const CassandraRow& row, size_t* index, bool use_keys,
      ColumnsTuple* data) const {
    const bool this_is_key = is_key(column_names_[I], key_names_);

    if (this_is_key == use_keys) {
      row.Get(*index, &get<I>(*data));
      ++*index;
    }

    ReadValues<I + 1>(row, index, use_keys, data);
  }

  void DoReadValues(const CassandraRow& row, ColumnsTuple* data) const {
    size_t i = 0;
    // Read keys.
    ReadValues<0>(row, &i, true /* use_keys */, data);
    // Read values.
    ReadValues<0>(row, &i, false /* use_keys */, data);
  }

  // Strings for CQL requests.

  static string create_table_str(
      const string& table, const StringVec& columns, const StringVec& keys, bool transactional) {
    CHECK_GT(columns.size(), 0);
    CHECK_GT(keys.size(), 0);
    CHECK_GE(columns.size(), keys.size());

    StringVec types;
    do_get_type_names(&types, ColumnsTuple());
    CHECK_EQ(types.size(), columns.size());

    for (size_t i = 0; i < columns.size(); ++i) {
      types[i] = columns[i] + ' ' + types[i];
    }


    return Format("CREATE TABLE IF NOT EXISTS $0 ($1, PRIMARY KEY ($2))$3;",
                  table, JoinStrings(types, ", "), JoinStrings(keys, ", "),
                  transactional ? " WITH transactions = { 'enabled' : true }" : "");
  }

  static string insert_with_bindings_str(const string& table, const StringVec& columns) {
    CHECK_GT(columns.size(), 0);
    const StringVec values(columns.size(), "?");

    return "INSERT INTO " + table + " (" +
        JoinStrings(columns, ", ") + ") VALUES (" + JoinStrings(values, ", ") + ");";
  }

  static string update_with_bindings_str(
      const string& table, const StringVec& columns, const StringVec& keys) {
    CHECK_GT(columns.size(), 0);
    CHECK_GT(keys.size(), 0);
    CHECK_GE(columns.size(), keys.size());
    StringVec values, key_values;

    for (const string& col : columns) {
      (is_key(col, keys) ? key_values : values).push_back(col + " = ?");
    }

    return "UPDATE " + table + " SET " +
        JoinStrings(values, ", ") + " WHERE " + JoinStrings(key_values, ", ") + ";";
  }

  static string select_with_bindings_str(const string& table, const StringVec& keys) {
    CHECK_GT(keys.size(), 0);
    StringVec key_values = keys;

    for (string& k : key_values) {
       k += " = ?";
    }

    return "SELECT * FROM " + table + " WHERE " + JoinStrings(key_values, " AND ") + ";";
  }

  static string select_by_token_str(const string& table, const StringVec& keys) {
    CHECK_GT(keys.size(), 0);
    return "SELECT * FROM " + table + " WHERE TOKEN(" + JoinStrings(keys, ", ") + ") = ?;";
  }

  static bool is_key(const string& name, const StringVec& keys) {
    return find(keys.begin(), keys.end(), name) != keys.end();
  }

 protected:
  string table_name_;
  StringVec column_names_;
  StringVec key_names_;
};

//------------------------------------------------------------------------------

template<size_t I, class Tuple>
typename std::enable_if<I >= std::tuple_size<Tuple>::value, void>::type
ExpectEqualTuplesHelper(const Tuple&, const Tuple&) {}

template<size_t I, class Tuple>
typename std::enable_if<I < std::tuple_size<Tuple>::value, void>::type
ExpectEqualTuplesHelper(const Tuple& t1, const Tuple& t2) {
  EXPECT_EQ(get<I>(t1), get<I>(t2));
  LOG(INFO) << "COMPARE: " << get<I>(t1) << " == " << get<I>(t2);
  ExpectEqualTuplesHelper<I + 1>(t1, t2);
}

template <class Tuple>
void ExpectEqualTuples(const Tuple& t1, const Tuple& t2) {
  ExpectEqualTuplesHelper<0>(t1, t2);
}

void LogResult(const CassandraResult& result) {
  auto iterator = result.CreateIterator();
  int i = 0;
  while (iterator.Next()) {
    ++i;
    std::string line;
    auto row = iterator.Row();
    auto row_iterator = row.CreateIterator();
    bool first = true;
    while (row_iterator.Next()) {
      if (first) {
        first = false;
      } else {
        line += ", ";
      }
      line += row_iterator.Value().ToString();
    }
    LOG(INFO) << i << ") " << line;
  }
}

//------------------------------------------------------------------------------

TEST_F(CppCassandraDriverTest, TestBasicTypes) {
  typedef TestTable<
      string, cass_bool_t, cass_float_t, cass_double_t, cass_int32_t, cass_int64_t, string> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, "test.basic", {"key", "bln", "flt", "dbl", "i32", "i64", "str"}, {"key"}));

  const MyTable::ColumnsTuple input("test", cass_true, 11.01f, 22.002, 3, 4, "text");
  table.Insert(&session_, input);

  MyTable::ColumnsTuple output("test", cass_false, 0.f, 0., 0, 0, "");
  table.SelectOneRow(&session_, &output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);
}

TEST_F(CppCassandraDriverTest, TestJsonBType) {
  typedef TestTable<string, CassandraJson> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.json", {"key", "json"}, {"key"}));

  MyTable::ColumnsTuple input("test", CassandraJson("{\"a\":1}"));
  table.Insert(&session_, input);

  MyTable::ColumnsTuple output("test", CassandraJson(""));
  table.SelectOneRow(&session_, &output);
  table.Print("RESULT OUTPUT", output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);

  get<1>(input) = CassandraJson("{\"b\":1}"); // 'json'
  table.Update(&session_, input);

  MyTable::ColumnsTuple updated_output("test", CassandraJson(""));
  table.SelectOneRow(&session_, &updated_output);
  table.Print("UPDATED RESULT OUTPUT", updated_output);

  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, updated_output);
}

void VerifyLongJson(const string& json) {
  // Parse JSON.
  JsonReader r(json);
  ASSERT_OK(r.Init());
  const Value* json_obj = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());

  EXPECT_TRUE(json_obj->HasMember("b"));
  EXPECT_EQ(rapidjson::kNumberType, (*json_obj)["b"].GetType());
  EXPECT_EQ(1, (*json_obj)["b"].GetInt());

  EXPECT_TRUE(json_obj->HasMember("a1"));
  EXPECT_EQ(rapidjson::kArrayType, (*json_obj)["a1"].GetType());
  const Value::ConstArray arr = (*json_obj)["a1"].GetArray();

  EXPECT_EQ(rapidjson::kNumberType, arr[2].GetType());
  EXPECT_EQ(3., arr[2].GetDouble());

  EXPECT_EQ(rapidjson::kFalseType, arr[3].GetType());
  EXPECT_EQ(false, arr[3].GetBool());

  EXPECT_EQ(rapidjson::kTrueType, arr[4].GetType());
  EXPECT_EQ(true, arr[4].GetBool());

  EXPECT_EQ(rapidjson::kObjectType, arr[5].GetType());
  const Value::ConstObject obj = arr[5].GetObject();
  EXPECT_TRUE(obj.HasMember("k2"));
  EXPECT_EQ(rapidjson::kArrayType, obj["k2"].GetType());
  EXPECT_EQ(rapidjson::kNumberType, obj["k2"].GetArray()[1].GetType());
  EXPECT_EQ(200, obj["k2"].GetArray()[1].GetInt());

  EXPECT_TRUE(json_obj->HasMember("a"));
  EXPECT_EQ(rapidjson::kObjectType, (*json_obj)["a"].GetType());
  const Value::ConstObject obj_a = (*json_obj)["a"].GetObject();

  EXPECT_TRUE(obj_a.HasMember("q"));
  EXPECT_EQ(rapidjson::kObjectType, obj_a["q"].GetType());
  const Value::ConstObject obj_q = obj_a["q"].GetObject();
  EXPECT_TRUE(obj_q.HasMember("s"));
  EXPECT_EQ(rapidjson::kNumberType, obj_q["s"].GetType());
  EXPECT_EQ(2147483647, obj_q["s"].GetInt());

  EXPECT_TRUE(obj_a.HasMember("f"));
  EXPECT_EQ(rapidjson::kStringType, obj_a["f"].GetType());
  EXPECT_EQ("hello", string(obj_a["f"].GetString()));
}

TEST_F(CppCassandraDriverTest, TestLongJson) {
  const string long_json =
      "{ "
        "\"b\" : 1,"
        "\"a2\" : {},"
        "\"a3\" : \"\","
        "\"a1\" : [1, 2, 3.0, false, true, { \"k1\" : 1, \"k2\" : [100, 200, 300], \"k3\" : true}],"
        "\"a\" :"
        "{"
          "\"d\" : true,"
          "\"q\" :"
            "{"
              "\"p\" : 4294967295,"
              "\"r\" : -2147483648,"
              "\"s\" : 2147483647"
            "},"
          "\"g\" : -100,"
          "\"c\" : false,"
          "\"f\" : \"hello\","
          "\"x\" : 2.0,"
          "\"y\" : 9223372036854775807,"
          "\"z\" : -9223372036854775808,"
          "\"u\" : 18446744073709551615,"
          "\"l\" : 2147483647.123123e+75,"
          "\"e\" : null"
        "}"
      "}";


  typedef TestTable<string, CassandraJson> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "basic", {"key", "json"}, {"key"}));

  MyTable::ColumnsTuple input("test", CassandraJson(long_json));
  table.Insert(&session_, input);

  ASSERT_OK(session_.ExecuteQuery(
      "INSERT INTO basic(key, json) values ('test0', '" + long_json + "');"));
  ASSERT_OK(session_.ExecuteQuery(
      "INSERT INTO basic(key, json) values ('test1', '{ \"a\" : 1 }');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test2', '\"abc\"');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test3', '3');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test4', 'true');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test5', 'false');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test6', 'null');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test7', '2.0');"));
  ASSERT_OK(session_.ExecuteQuery("INSERT INTO basic(key, json) values ('test8', '{\"b\" : 1}');"));

  for (auto key : {"test"s, "test0"s} ) {
    MyTable::ColumnsTuple output(key, CassandraJson(""));
    table.SelectOneRow(&session_, &output);
    table.Print("RESULT OUTPUT", output);

    LOG(INFO) << "Checking selected JSON object for key=" << key;
    const string json = get<1>(output).value(); // 'json'

    ASSERT_EQ(json,
        "{"
          "\"a\":"
          "{"
            "\"c\":false,"
            "\"d\":true,"
            "\"e\":null,"
            "\"f\":\"hello\","
            "\"g\":-100,"
            "\"l\":2.147483647123123e84,"
            "\"q\":"
            "{"
              "\"p\":4294967295,"
              "\"r\":-2147483648,"
              "\"s\":2147483647"
            "},"
            "\"u\":18446744073709551615,"
            "\"x\":2.0,"
            "\"y\":9223372036854775807,"
            "\"z\":-9223372036854775808"
          "},"
          "\"a1\":[1,2,3.0,false,true,{\"k1\":1,\"k2\":[100,200,300],\"k3\":true}],"
          "\"a2\":{},"
          "\"a3\":\"\","
          "\"b\":1"
        "}");

    VerifyLongJson(json);
  }
}

namespace {

Result<IndexPermissions> TestBackfillCreateIndexTableSimple(CppCassandraDriverTestIndex* test) {
  return TestBackfillCreateIndexTableSimple(
      test, false, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

Status ExecuteQueryWithRetriesOnSchemaMismatch(CassandraSession* session, const string& query) {
  return LoggedWaitFor(
      [session, &query]() -> Result<bool> {
        auto result = session->ExecuteQuery(query);
        if (result.ok()) {
          return true;
        }
        if (result.IsQLError() &&
            result.message().ToBuffer().find("Wrong Metadata Version.") != string::npos) {
          return false;  // Retry.
        }
        return result;
      },
      MonoDelta::FromSeconds(90), yb::Format("Retrying query: $0", query));
}

}  // namespace

Result<IndexPermissions> TestBackfillCreateIndexTableSimple(
    CppCassandraDriverTestIndex* test, bool deferred, IndexPermissions target_permission) {
  TestTable<cass_int32_t, string> table;
  RETURN_NOT_OK(table.CreateTable(&test->session_, "test.test_table",
                                  {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Inserting one row";
  RETURN_NOT_OK(test->session_.ExecuteQuery(
      "insert into test_table (k, v) values (1, 'one');"));
  LOG(INFO) << "Creating index";
  WARN_NOT_OK(test->session_.ExecuteQuery(yb::Format(
                  "create $0 index test_table_index_by_v on test_table (v);",
                  (deferred ? "deferred" : ""))),
              "create-index failed.");

  LOG(INFO) << "Inserting two rows";
  RETURN_NOT_OK(ExecuteQueryWithRetriesOnSchemaMismatch(
      &test->session_, "insert into test_table (k, v) values (2, 'two');"));
  RETURN_NOT_OK(ExecuteQueryWithRetriesOnSchemaMismatch(
      &test->session_, "insert into test_table (k, v) values (3, 'three');"));

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");
  return test->client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, target_permission);
}

TEST_F_EX(CppCassandraDriverTest, TestCreateIndex, CppCassandraDriverTestIndexSlow) {
  IndexPermissions perm = ASSERT_RESULT(TestBackfillCreateIndexTableSimple(this));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(CppCassandraDriverTest, TestCreateIndexDeferred, CppCassandraDriverTestIndex) {
  IndexPermissions perm = ASSERT_RESULT(
      TestBackfillCreateIndexTableSimple(this, true, IndexPermissions::INDEX_PERM_DO_BACKFILL));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");
  // Sleep a little and check again. We expect no further progress.
  const size_t kSleepTimeMs = 10000;
  SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
  perm = ASSERT_RESULT(client_->GetIndexPermissions(table_name, index_table_name));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);

  // create another index without backfill being deferred. Both the indexes should backfill
  // and go to INDEX_PERM_READ_WRITE_AND_DELETE.
  auto s = session_.ExecuteQuery("create index test_table_index_by_v_2 on test_table(v);");
  const YBTableName index_table_name2(YQL_DATABASE_CQL, "test", "test_table_index_by_v_2");

  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name2, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  perm = ASSERT_RESULT(client_->GetIndexPermissions(table_name, index_table_name));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(CppCassandraDriverTest, TestDeferredIndexBackfillsAfterWait,
          CppCassandraDriverTestIndex) {
  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");

  IndexPermissions perm = ASSERT_RESULT(
      TestBackfillCreateIndexTableSimple(this, /* deferred */ true,
                                         IndexPermissions::INDEX_PERM_DO_BACKFILL));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);

  // Ensure there is no progress even if we wait for a while.
  constexpr auto kWaitSec = 10;
  SleepFor(MonoDelta::FromSeconds(kWaitSec));
  perm = ASSERT_RESULT(client_->GetIndexPermissions(table_name, index_table_name));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);

  // Launch Backfill through yb-admin
  constexpr auto kAdminRpcTimeout = 5;
  auto yb_admin_client = std::make_unique<tools::enterprise::ClusterAdminClient>(
      cluster_->GetMasterAddresses(), MonoDelta::FromSeconds(kAdminRpcTimeout));
  ASSERT_OK(yb_admin_client->Init());
  ASSERT_OK(yb_admin_client->LaunchBackfillIndexForTable(table_name));

  // Confirm that the backfill should proceed to completion.
  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateIndexSlowTServer,
    CppCassandraDriverTestIndexNonResponsiveTServers) {
  // We expect the create index to fail.
  auto res = TestBackfillCreateIndexTableSimple(this);
  if (res.ok()) {
    ASSERT_NE(*res, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  } else if (res.status().IsTimedOut()) {
    // It was probably on NotFound retry loop, so just send some request to the index and expect
    // NotFound.  See issue #5932 to alleviate the need to do this.
    const YBTableName index_table_name(YQL_DATABASE_CQL, "test", "test_table_index_by_v");
    auto res2 = client_->GetYBTableInfo(index_table_name);
    ASSERT_TRUE(!res2.ok());
    ASSERT_TRUE(res2.status().IsNotFound()) << res2.status();
  } else {
    ASSERT_TRUE(res.status().IsNotFound()) << res.status();
  }
}

Result<int64_t> GetTableSize(CassandraSession *session, const std::string& table_name) {
  int64_t size = 0;
  RETURN_NOT_OK(session->ExecuteAndProcessOneRow(
      Format("select count(*) from $0;", table_name),
      [&size](const CassandraRow& row) {
        size = row.Value(0).As<int64_t>();
      }));
  return size;
}

void TestBackfillIndexTable(
    CppCassandraDriverTestIndex* test, PKOnlyIndex is_pk_only,
    IsUnique is_unique = IsUnique::kFalse,
    IncludeAllColumns include_primary_key = IncludeAllColumns::kFalse,
    UserEnforced user_enforced = UserEnforced::kFalse) {
  constexpr int kLoops = 3;
  constexpr size_t kBatchSize = 10;
  constexpr size_t kNumBatches = 10;
  constexpr size_t kExpectedCount = kBatchSize * kNumBatches;

  typedef TestTable<string, string, string> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&test->session_, "test.key_value",
                              {"key1", "key2", "value"}, {"(key1, key2)"},
                              !user_enforced));


  LOG(INFO) << "Creating index";
  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "key_value");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "index_by_value");

  std::vector<CassandraFuture> futures;

  int num_failures = 0;
  CassandraFuture create_index_future(nullptr);
  for (int loop = 1; loop <= kLoops; ++loop) {
    for (int batch_idx = 0; batch_idx != kNumBatches; ++batch_idx) {
      CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
      auto prepared = table.PrepareInsert(&test->session_);
      if (!prepared.ok()) {
        // Prepare could be failed because cluster has heavy load.
        // It is ok to just retry in this case, because we check that process did not crash.
        continue;
      }
      for (int i = 0; i != kBatchSize; ++i) {
        const int key = batch_idx * kBatchSize + i;
        // For non-unique tests, the value will be of the form v-l0xx where l is
        // the loop number, and xx is the key.
        // For unique index tests, the value will be a permutation of
        //  1 .. kExpectedCount; or -1 .. -kExpectedCount for odd and even
        //  loops.
        const int value =
            (is_unique ? (loop % 2 ? 1 : -1) *
                             ((loop * 1000 + key) % kExpectedCount + 1)
                       : loop * 1000 + key);
        ColumnsType tuple(Format("k-$0", key), Format("k-$0", key),
                          Format("v-$0", value));
        auto statement = prepared->Bind();
        table.BindInsert(&statement, tuple);
        batch.Add(&statement);
      }
      futures.push_back(test->session_.SubmitBatch(batch));
    }

    // For unique index tests, we want to make sure each loop of writes is
    // complete before issuing the next one. For non-unique index tests,
    // we only wait for the writes to persist before issuing the
    // create index command.
    if (is_unique || loop == 2) {
      // Let us make sure that the writes so far have persisted.
      for (auto& future : futures) {
        if (!future.Wait().ok()) {
          num_failures++;
        }
      }
      futures.clear();
    }

    // At the end of the second loop, we will issue the create index.
    // The remaining loop(s) of writes will be concurrent with the create index.
    if (loop == 2) {
      create_index_future = test->session_.ExecuteGetFuture(
          Format("create $0 index index_by_value on test.key_value ($1) $2 $3;",
                 (is_unique ? "unique" : ""), (is_pk_only ? "key2" : "value"),
                 (include_primary_key ? "include (key1, key2, value)" : " "),
                 (user_enforced ? "with transactions = { 'enabled' : false,"
                                  "'consistency_level' : 'user_enforced' }"
                                : "")));
    }
  }

  for (auto& future : futures) {
    auto res = future.Wait();
    if (!res.ok()) {
      num_failures++;
    }
    WARN_NOT_OK(res, "Write batch failed: ")
  }
  if (num_failures > 0) {
    LOG(INFO) << num_failures << " write batches failed.";
  }

  // It is fine for user enforced create index to timeout because
  // index_backfill_upperbound_for_user_enforced_txn_duration_ms is longer than
  // client_read_write_timeout_ms
  auto s = create_index_future.Wait();
  WARN_NOT_OK(s, "Create index failed.");

  const auto kLowerBound = kExpectedCount - kBatchSize * num_failures;
  const auto kUpperBound = kExpectedCount + kBatchSize * num_failures;

  // Verified implicitly here that the backfill job has met the expected total number of
  // records
  const auto kMaxWait = kTimeMultiplier * 60s;
  WARN_NOT_OK(WaitForBackfillSafeTimeOn(test->cluster_.get(), table_name, kMaxWait),
      "Could not get safe time. May be OK, if the backfill is already done.");
  WARN_NOT_OK(WaitForBackfillSatisfyCondition(
      test->cluster_->GetMasterProxy<master::MasterDdlProxy>(), table_name,
      [kLowerBound](Result<master::BackfillJobPB> backfill_job) -> Result<bool> {
        if (!backfill_job) {
          return backfill_job.status();
        }
        const auto number_rows_processed = backfill_job->num_rows_processed();
        return number_rows_processed >= kLowerBound;
      }, kMaxWait),
      "Could not get BackfillJobPB. May be OK, if the backfill is already done.");

  IndexPermissions perm = ASSERT_RESULT(test->client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE, kMaxWait));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  auto main_table_size = ASSERT_RESULT(GetTableSize(&test->session_, "key_value"));
  auto index_table_size = ASSERT_RESULT(GetTableSize(&test->session_, "index_by_value"));

  EXPECT_GE(main_table_size, kLowerBound);
  EXPECT_LE(main_table_size, kUpperBound);
  EXPECT_GE(index_table_size, kLowerBound);
  EXPECT_LE(index_table_size, kUpperBound);
  if (!user_enforced || num_failures == 0) {
    EXPECT_EQ(main_table_size, index_table_size);
  }
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateIndex, CppCassandraDriverTestIndexSlow) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kFalse,
                         IncludeAllColumns::kFalse);
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateIndexPKOnly, CppCassandraDriverTestIndexSlow) {
  TestBackfillIndexTable(this, PKOnlyIndex::kTrue, IsUnique::kFalse,
                         IncludeAllColumns::kFalse);
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateIndexCovered, CppCassandraDriverTestIndexSlow) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kFalse,
                         IncludeAllColumns::kTrue);
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateIndexUserEnforced,
          CppCassandraDriverTestUserEnforcedIndex) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kFalse,
                         IncludeAllColumns::kTrue, UserEnforced::kTrue);
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateUniqueIndex, CppCassandraDriverTestIndexSlow) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kTrue,
                         IncludeAllColumns::kFalse);
}

TEST_F_EX(
    CppCassandraDriverTest, TestTableCreateUniqueIndexCovered, CppCassandraDriverTestIndexSlow) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kTrue,
                         IncludeAllColumns::kTrue);
}

TEST_F_EX(CppCassandraDriverTest, TestTableCreateUniqueIndexUserEnforced,
          CppCassandraDriverTestUserEnforcedIndex) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kTrue,
                         IncludeAllColumns::kTrue, UserEnforced::kTrue);
}

bool CreateTableSuccessOrTimedOut(const Status& s) {
  // We sometimes get a Runtime Error from cql_test_util wrapping the actual Timeout.
  return s.ok() || s.IsTimedOut() ||
         string::npos != s.ToUserMessage().find("Timed out waiting for Table Creation");
}

TEST_F_EX(CppCassandraDriverTest, WaitForSplitsToComplete, CppCassandraDriverTestIndexSlow) {
  typedef TestTable<int, int> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  constexpr int kBatchSize = 3000;
  const std::string kNamespace = "test";
  const std::string kTableName = "test_table";
  const std::string kIndexName = "test_table_index_by_v";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, kTableName);
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, kIndexName);
  ASSERT_OK(table.CreateTable(&session_, kNamespace + "." + kTableName, {"k", "v"}, {"(k)"}, true));

  CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
  auto prepared = ASSERT_RESULT(table.PrepareInsert(&session_));
  for (int i = 0; i < kBatchSize; ++i) {
    ColumnsType tuple(i, i);
    auto statement = prepared.Bind();
    table.BindInsert(&statement, tuple);
    batch.Add(&statement);
  }
  ASSERT_OK(session_.ExecuteBatch(batch));

  const TabletId tablet_to_split = ASSERT_RESULT(GetSingleTabletId(kTableName));
  // Flush the written data to generate SST files that can be split.
  const std::string table_id = ASSERT_RESULT(GetTableIdByTableName(
      client_.get(), kNamespace, kTableName));

  ASSERT_OK(client_->FlushTables(
      {table_id},
      false /* add_indexes */,
      3 /* timeout_secs */,
      false /* is_compaction */));

  // Create a split that will not complete until we set the test flag to true.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pause_tserver_get_split_key", "true"));
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>();
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_to_split);
  master::SplitTabletResponsePB resp;
  rpc::RpcController rpc;
  rpc::RpcController controller;
  ASSERT_OK(proxy.SplitTablet(req, &resp, &controller));

  // Create index command should succeed since it is async, but the index should fail to backfill
  // while there is an ongoing split.
  ASSERT_OK(session_.ExecuteQuery(Format("create index $0 on $1 (v);", kIndexName, kTableName)));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto table_info = VERIFY_RESULT(client_->GetYBTableInfo(table_name));
    VERIFY_EQ(table_info.index_map.size(), 1UL);
    return !table_info.index_map.begin()->second.backfill_error_message().empty();
  }, FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1s + 5s * kTimeMultiplier,
     "Waiting for backfill to fail."));

  // Drop the failed index so we can try to create the index again.
  ASSERT_OK(session_.ExecuteQuery(Format("drop index $0;", kIndexName)));

  // Ongoing split which should finish within the threshold of
  // FLAGS_index_backfill_tablet_split_completion_timeout_sec, so the backfill should succeed this
  // time.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pause_tserver_get_split_key", "false"));
  ASSERT_OK(session_.ExecuteQuery(Format("create index $0 on $1 (v);", kIndexName, kTableName)));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto perm = VERIFY_RESULT(client_->GetIndexPermissions(table_name, index_table_name));
    return perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE;
  }, FLAGS_index_backfill_tablet_split_completion_timeout_sec * 1s,
     "Backfill did not complete in time."));

  auto main_table_size = ASSERT_RESULT(GetTableSize(&session_, kTableName));
  auto index_table_size = ASSERT_RESULT(GetTableSize(&session_, kIndexName));
  ASSERT_EQ(main_table_size, index_table_size);
}

TEST_F_EX(CppCassandraDriverTest, TestCreateJsonbIndex, CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, CassandraJson> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (1, '{\"f1\": \"one\", \"f2\": \"one\"}');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (2, '{\"f1\": \"two\", \"f2\": \"two\"}');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (3, '{\"f1\": \"three\", \"f2\": \"three\"}');"));

  LOG(INFO) << "Creating index";
  auto s = session_.ExecuteQuery(
      "create unique index test_table_index_by_v_f1 on test_table (v->>'f1');");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v_f1");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  auto main_table_size =
      ASSERT_RESULT(GetTableSize(&session_, "test_table"));
  auto index_table_size =
      ASSERT_RESULT(GetTableSize(&session_, "test_table_index_by_v_f1"));
  ASSERT_EQ(main_table_size, index_table_size);
}

TEST_F_EX(CppCassandraDriverTest, TestCreateUniqueIndexPasses, CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (1, 'one');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (2, 'two');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (3, 'three');"));

  LOG(INFO) << "Creating index";
  auto s = session_.ExecuteQuery(
      "create unique index test_table_index_by_v on test_table (v);");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  LOG(INFO) << "Inserting more rows -- collisions will be detected.";
  ASSERT_TRUE(!session_
                   .ExecuteGetFuture(
                       "insert into test_table (k, v) values (-1, 'one');")
                   .Wait()
                   .ok());
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (4, 'four');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (5, 'five');"));
  ASSERT_TRUE(!session_
                   .ExecuteGetFuture(
                       "insert into test_table (k, v) values (-4, 'four');")
                   .Wait()
                   .ok());
}

class CppCassandraDriverTestRestart : public CppCassandraDriverTest {
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--cql_prepare_child_threshold_ms=5000"};
  }
};

TEST_F_EX(CppCassandraDriverTest, TestCreateUniqueIndexPartial, CppCassandraDriverTestRestart) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Creating index";
  auto s = session_.ExecuteQuery(
      "create unique index test_table_index_by_v on test_table (v) where k > 0;");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  const size_t kNumTServers = cluster_->num_tablet_servers();
  cluster_->Shutdown(ExternalMiniCluster::NodeSelectionMode::TS_ONLY);
  ASSERT_OK(cluster_->Restart());
  const auto kMaxWaitTime = 5s;
  ASSERT_OK(cluster_->WaitForTabletServerCount(kNumTServers, kMaxWaitTime));
  for (size_t i = 0; i < kNumTServers; i++) {
    ASSERT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(i), kMaxWaitTime));
  }
  session_ = CHECK_RESULT(driver_->CreateSession());

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(session_.ExecuteQuery("insert into test.test_table (k, v) values (1, 'one');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test.test_table (k, v) values (2, 'two');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test.test_table (k, v) values (3, 'three');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test.test_table (k, v) values (-1, 'one');"));

  LOG(INFO) << "Inserting more rows -- collisions will be detected.";
  Status status = session_.ExecuteQuery("insert into test.test_table (k, v) values (13, 'three');");
  ASSERT_TRUE(status.message().ToBuffer().find("Duplicate value") != std::string::npos);
  ASSERT_OK(session_.ExecuteQuery("insert into test.test_table (k, v) values (-3, 'three');"));
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateUniqueIndexPartialWithRestart,
    CppCassandraDriverTestRestart) {
  ASSERT_OK(session_.ExecuteQuery("use test;"));
  ASSERT_OK(session_.ExecuteQuery(
      "CREATE TABLE test.multindex ("
      "i int, s1 text, s2 text, s3 text, s4 text, s5 text, s6 text, s7 text, s8 text,"
      "PRIMARY KEY ((i, s1), s2, s3, s4)) WITH CLUSTERING ORDER BY (s2 ASC, s3 ASC, s4 ASC)"
      "AND default_time_to_live = 0"
      "AND tablets = 1"
      "AND transactions = {'enabled': 'true'};"));
  ASSERT_OK(
      session_.ExecuteQuery("CREATE UNIQUE INDEX uidx1 ON test.multindex ((i, s4), s2, s3) INCLUDE "
                            "(s1, s5, s6) WHERE s2 != '' and s3 != '' and i=1   with tablets=1;"));
  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "multindex");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "uidx1");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_TRUE(perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '1', '1','1','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (2, '1', '1','1','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (2, '2', '1','1','1','1','1');"));
  // error expected
  Status status = session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '2', '1','1','1','1','1');");
  ASSERT_TRUE(status.message().ToBuffer().find("Duplicate value") != std::string::npos);

  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '2', '','1','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '2', '','','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '4', '','','1','1','1');"));

  const size_t kNumTServers = cluster_->num_tablet_servers();
  cluster_->Shutdown(ExternalMiniCluster::NodeSelectionMode::TS_ONLY);
  ASSERT_OK(cluster_->Restart());
  const auto kMaxWaitTime = 5s;
  ASSERT_OK(cluster_->WaitForTabletServerCount(kNumTServers, kMaxWaitTime));
  for (size_t i = 0; i < kNumTServers; i++) {
    ASSERT_OK(cluster_->WaitForTabletsRunning(cluster_->tablet_server(i), kMaxWaitTime));
  }
  session_ = CHECK_RESULT(driver_->CreateSession());

  // after restart
  ASSERT_OK(session_.ExecuteQuery("use test;"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '45', '','','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '5', '','','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '6', '','','1','1','1');"));

  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (2, '6', '','','1','1','1');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into multindex (i, s1, s2, s3, s4, s5, s6) values (1, '1', '','','1','1','1');"));
}

TEST_F_EX(CppCassandraDriverTest, TestCreateUniqueIndexIntent, CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, cass_int32_t> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  constexpr int kNumRows = 10;
  LOG(INFO) << "Inserting " << kNumRows << " rows";
  for (int i = 1; i <= kNumRows; i++) {
    ASSERT_OK(session_.ExecuteQuery(
        Substitute("insert into test_table (k, v) values ($0, $0);", i)));
  }

  LOG(INFO) << "Creating index";
  auto session2 = CHECK_RESULT(EstablishSession());
  CassandraFuture create_index_future = session2.ExecuteGetFuture(
      "create unique index test_table_index_by_v on test_table (v);");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name,
      IndexPermissions::INDEX_PERM_WRITE_AND_DELETE,
      50ms /* max_wait */));
  if (perm != IndexPermissions::INDEX_PERM_WRITE_AND_DELETE) {
    LOG(WARNING) << "IndexPermissions is already past WRITE_AND_DELETE. "
                 << "This run of the test may not actually be doing anything "
                    "non-trivial.";
  }

  const size_t kSleepTimeMs = 20;
  LOG(INFO) << "Inserting " << kNumRows / 2 << " rows again.";
  for (int i = 1; i < kNumRows / 2; i++) {
    if (session_
            .ExecuteQuery(Substitute("delete from test_table where k=$0;", i))
            .ok()) {
      WARN_NOT_OK(session_.ExecuteQuery(Substitute(
                      "insert into test_table (k, v) values ($0, $0);", i)),
                  "Overwrite failed");
      SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
    } else {
      LOG(ERROR) << "Deleting & Inserting failed for " << i;
    }
  }

  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name,
      IndexPermissions::INDEX_PERM_DO_BACKFILL,
      50ms /* max_wait */));
  if (perm != IndexPermissions::INDEX_PERM_DO_BACKFILL) {
    LOG(WARNING) << "IndexPermissions already past DO_BACKFILL";
  }

  LOG(INFO) << "Inserting " << kNumRows / 2 << " more rows again.";
  for (int i = kNumRows / 2; i <= kNumRows; i++) {
    if (session_
            .ExecuteQuery(Substitute("delete from test_table where k=$0;", i))
            .ok()) {
      WARN_NOT_OK(session_.ExecuteQuery(Substitute(
                      "insert into test_table (k, v) values (-$0, $0);", i)),
                  "Overwrite failed");
      SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
    } else {
      LOG(ERROR) << "Deleting & Inserting failed for " << i;
    }
  }

  LOG(INFO) << "Waited on the Create Index to finish. Status  = "
            << create_index_future.Wait();

  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateUniqueIndexPassesManyWrites,
    CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  constexpr int kNumRows = 100;
  LOG(INFO) << "Inserting " << kNumRows << " rows";
  for (int i = 1; i <= kNumRows; i++) {
    ASSERT_OK(session_.ExecuteQuery(
        Substitute("insert into test_table (k, v) values ($0, 'v-$0');", i)));
  }

  LOG(INFO) << "Creating index";
  auto session2 = ASSERT_RESULT(EstablishSession());
  CassandraFuture create_index_future = session2.ExecuteGetFuture(
      "create unique index test_table_index_by_v on test_table (v);");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name,
      IndexPermissions::INDEX_PERM_WRITE_AND_DELETE,
      50ms /* max_wait */));
  if (perm != IndexPermissions::INDEX_PERM_WRITE_AND_DELETE) {
    LOG(WARNING) << "IndexPermissions is already past WRITE_AND_DELETE. "
                 << "This run of the test may not actually be doing anything "
                    "non-trivial.";
  }

  const size_t kSleepTimeMs = 20;
  LOG(INFO) << "Inserting " << kNumRows / 2 << " rows again.";
  for (int i = 1; i < kNumRows / 2; i++) {
    if (session_
            .ExecuteQuery(Substitute("delete from test_table where k=$0;", i))
            .ok()) {
      WARN_NOT_OK(
          session_.ExecuteQuery(Substitute(
              "insert into test_table (k, v) values (-$0, 'v-$0');", i)),
          "Overwrite failed");
      SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
    } else {
      LOG(ERROR) << "Deleting & Inserting failed for " << i;
    }
  }

  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name,
      IndexPermissions::INDEX_PERM_DO_BACKFILL,
      50ms /* max_wait */));
  if (perm != IndexPermissions::INDEX_PERM_DO_BACKFILL) {
    LOG(WARNING) << "IndexPermissions already past DO_BACKFILL";
  }

  LOG(INFO) << "Inserting " << kNumRows / 2 << " more rows again.";
  for (int i = kNumRows / 2; i <= kNumRows; i++) {
    if (session_
            .ExecuteQuery(Substitute("delete from test_table where k=$0;", i))
            .ok()) {
      WARN_NOT_OK(
          session_.ExecuteQuery(Substitute(
              "insert into test_table (k, v) values (-$0, 'v-$0');", i)),
          "Overwrite failed");
      SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
    } else {
      LOG(ERROR) << "Deleting & Inserting failed for " << i;
    }
  }

  LOG(INFO) << "Waited on the Create Index to finish. Status  = "
            << create_index_future.Wait();

  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateIdxTripleCollisionTest, CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  ASSERT_OK(
      session_.ExecuteQuery("insert into test_table (k, v) values (1, 'a')"));
  ASSERT_OK(
      session_.ExecuteQuery("insert into test_table (k, v) values (3, 'a')"));
  ASSERT_OK(
      session_.ExecuteQuery("insert into test_table (k, v) values (4, 'a')"));

  LOG(INFO) << "Creating index";
  // session_.ExecuteQuery("create unique index test_table_index_by_v on
  // test_table (v);");
  auto session2 = ASSERT_RESULT(EstablishSession());
  CassandraFuture create_index_future = session2.ExecuteGetFuture(
      "create unique index test_table_index_by_v on test_table (v);");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  {
    IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_DELETE_ONLY,
        50ms /* max_wait */));
    EXPECT_EQ(perm, IndexPermissions::INDEX_PERM_DELETE_ONLY);
  }

  CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 90s,
                             CoarseMonoClock::Duration::max());
  auto res = session_.ExecuteQuery("DELETE from test_table WHERE k=4");
  LOG(INFO) << "Got " << yb::ToString(res);
  while (!res.ok()) {
    waiter.Wait();
    res = session_.ExecuteQuery("DELETE from test_table WHERE k=4");
    LOG(INFO) << "Got " << yb::ToString(res);
  }

  LOG(INFO) << "Waited on the Create Index to finish. Status  = "
            << create_index_future.Wait();
  {
    auto res = client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_NOT_USED,
        50ms /* max_wait */);
    ASSERT_TRUE(!res.ok());
    ASSERT_TRUE(res.status().IsNotFound());

    ASSERT_OK(LoggedWaitFor(
        [this, index_table_name]() {
          Result<YBTableInfo> index_table_info = client_->GetYBTableInfo(index_table_name);
          return !index_table_info && index_table_info.status().IsNotFound();
        },
        10s, "waiting for index to be deleted"));
  }
}

// Simulate this situation:
//   Session A                                    Session B
//   ------------------------------------         -------------------------------------------
//   CREATE TABLE (i, j, PRIMARY KEY (i))
//                                                INSERT (1, 'a')
//   CREATE UNIQUE INDEX (j)
//   - DELETE_ONLY perm
//                                                DELETE (1, 'a')
//                                                (delete (1, 'a') to index)
//                                                INSERT (2, 'a')
//   - WRITE_DELETE perm
//   - BACKFILL perm
//     - get safe time for read
//                                                INSERT (3, 'a')
//                                                (insert (3, 'a') to index)
//     - do the actual backfill
//                                                (insert (2, 'a') to index--detect conflict)
//   - READ_WRITE_DELETE perm
// This test is for issue #5811.
TEST_F_EX(
    CppCassandraDriverTest,
    YB_DISABLE_TEST_IN_SANITIZERS(CreateUniqueIndexWriteAfterSafeTime),
    CppCassandraDriverTestIndexSlower) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  ASSERT_OK(session_.ExecuteQuery("INSERT INTO test_table (k, v) VALUES (1, 'a')"));

  LOG(INFO) << "Creating index";
  auto session2 = ASSERT_RESULT(EstablishSession());
  CassandraFuture create_index_future = session2.ExecuteGetFuture(
      "CREATE UNIQUE INDEX test_table_index_by_v ON test_table (v)");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");

  LOG(INFO) << "Wait for DELETE permission";
  {
    // Deadline is
    //   3s for before WRITE perm sleep
    // + 3s for extra
    // = 6s
    // Right after the "before WRITE", the index permissions become WRITE while the fully applied
    // index permissions become DELETE.  (Actually, the index should already be fully applied DELETE
    // before this since that's the starting permission.)
    IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_DELETE_ONLY,
        CoarseMonoClock::Now() + 6s /* deadline */,
        50ms /* max_wait */));
    ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DELETE_ONLY);
  }

  LOG(INFO) << "Do insert and delete before WRITE permission";
  {
    // Deadline is
    //   3s for before WRITE perm sleep
    // + 3s for extra
    // = 6s
    // We want to make sure the latest permissions are not WRITE.  The latest permissions become
    // WRITE after "before WRITE".
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 6s, CoarseMonoClock::Duration::max());
    Status status;
    do {
      status = session_.ExecuteQuery("DELETE from test_table WHERE k = 1");
      LOG(INFO) << "Got " << yb::ToString(status);
      if (status.ok()) {
        status = session_.ExecuteQuery("INSERT INTO test_table (k, v) VALUES (2, 'a')");
      }
      ASSERT_TRUE(waiter.Wait());
    } while (!status.ok());
  }

  LOG(INFO) << "Ensure it is still DELETE permission";
  {
    IndexPermissions perm = ASSERT_RESULT(client_->GetIndexPermissions(
        table_name,
        index_table_name));
    ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DELETE_ONLY);
  }

  LOG(INFO) << "Wait for BACKFILL permission";
  {
    // Deadline is
    //   3s for before WRITE perm sleep
    // + 3s for after WRITE perm sleep
    // + 3s for before BACKFILL perm sleep
    // + 3s for after BACKFILL perm sleep
    // + 3s for extra
    // = 15s
    IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_DO_BACKFILL,
        CoarseMonoClock::Now() + 15s /* deadline */,
        50ms /* max_wait */));
    EXPECT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);
  }

  LOG(INFO) << "Wait to get safe time for backfill (currently approximated using 1s sleep)";
  SleepFor(1s);

  LOG(INFO) << "Do insert before backfill";
  {
    // Deadline is
    //   2s for remainder of 3s sleep of backfill
    // + 3s for extra
    // = 5s
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 5s, CoarseMonoClock::Duration::max());
    while (true) {
      Status status = session_.ExecuteQuery("INSERT INTO test_table (k, v) VALUES (3, 'a')");
      LOG(INFO) << "Got " << yb::ToString(status);
      if (status.ok()) {
        break;
      } else {
        ASSERT_FALSE(status.IsIllegalState() &&
                     status.message().ToBuffer().find("Duplicate value") != std::string::npos)
            << "The insert should come before backfill, so it should not cause duplicate conflict.";
        ASSERT_TRUE(waiter.Wait());
      }
    }
  }

  LOG(INFO) << "Wait for CREATE INDEX to finish (either succeed or fail)";
  bool is_index_created;
  {
    // Deadline is
    //   2s for remainder of 3s sleep of backfill
    // + 3s for before READ or WRITE_WHILE_REMOVING perm sleep
    // + 3s for after WRITE_WHILE_REMOVING perm sleep
    // + 3s for before DELETE_WHILE_REMOVING perm sleep
    // + 3s for extra
    // = 14s
    // (In the fail case) Right after the "before DELETE_WHILE_REMOVING", the index permissions
    // become DELETE_WHILE_REMOVING while the fully applied index permissions become
    // WRITE_WHILE_REMOVING, and WRITE_WHILE_REMOVING is the first permission in the fail case >=
    // READ permission.
    IndexPermissions perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
        CoarseMonoClock::Now() + 14s /* deadline */,
        50ms /* max_wait */));
    if (perm != IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE) {
      LOG(INFO) << "Wait for index to get deleted";
      auto result = client_->WaitUntilIndexPermissionsAtLeast(
          table_name,
          index_table_name,
          IndexPermissions::INDEX_PERM_NOT_USED,
          50ms /* max_wait */);
      ASSERT_TRUE(!result.ok());
      ASSERT_TRUE(result.status().IsNotFound());
      is_index_created = false;
    } else {
      is_index_created = true;
    }
  }

  // Check.
  {
    auto result = GetTableSize(&session_, "test_table");
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 10s, CoarseMonoClock::Duration::max());
    while (!result.ok()) {
      ASSERT_TRUE(waiter.Wait());
      ASSERT_TRUE(result.status().IsQLError()) << result.status();
      ASSERT_TRUE(result.status().message().ToBuffer().find("schema version mismatch")
                  != std::string::npos) << result.status();
      // Retry.
      result = GetTableSize(&session_, "test_table");
    }
    const int64_t main_table_size = *result;
    result = GetTableSize(&session_, "test_table_index_by_v");

    ASSERT_EQ(main_table_size, 2);
    if (is_index_created) {
      // This is to demonstrate issue #5811.  These statements should not fail.
      const int64_t index_table_size = ASSERT_RESULT(std::move(result));
      ASSERT_EQ(index_table_size, 1);
      // Since the main table has two rows while the index has one row, the index is inconsistent.
      ASSERT_TRUE(false) << "index was created and is inconsistent with its indexed table";
    } else {
      ASSERT_NOK(result);
    }
  }
}

class CppCassandraDriverTestSlowTServer : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    auto flags = CppCassandraDriverTest::ExtraTServerFlags();
    flags.push_back("--TEST_slowdown_backfill_by_ms=5000");
    flags.push_back("--ycql_num_tablets=1");
    flags.push_back("--ysql_num_tablets=1");
    return flags;
  }
};

TEST_F_EX(
    CppCassandraDriverTest,
    DeleteIndexWhileBackfilling,
    CppCassandraDriverTestSlowTServer) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Creating two indexes that will backfill together";
  // Create 2 indexes that backfill together. One of them will be deleted while the backfill
  // is happening. The deleted index should be successfully deleted, and the other index will
  // be successfully backfilled.
  auto session2 = ASSERT_RESULT(EstablishSession());
  CassandraFuture create_index_future0 =
      session2.ExecuteGetFuture("CREATE DEFERRED INDEX test_table_index_by_v0 ON test_table (v)");
  CassandraFuture create_index_future1 =
      session2.ExecuteGetFuture("CREATE INDEX test_table_index_by_v1 ON test_table (v)");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name0(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v0");
  const YBTableName index_table_name1(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v1");

  auto res = client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name1, IndexPermissions::INDEX_PERM_DO_BACKFILL, 50ms /* max_wait */);
  // Allow backfill to get past GetSafeTime
  ASSERT_OK(WaitForBackfillSafeTimeOn(cluster_.get(), table_name));

  ASSERT_OK(session_.ExecuteQuery("drop index test_table_index_by_v1"));

  // Wait for the backfill to actually run to completion/failure.
  SleepFor(MonoDelta::FromSeconds(10));
  res = client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name1, IndexPermissions::INDEX_PERM_NOT_USED, 50ms /* max_wait */);
  ASSERT_TRUE(!res.ok());
  ASSERT_TRUE(res.status().IsNotFound());

  auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name0, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
}

TEST_F_EX(CppCassandraDriverTest, TestPartialFailureDeferred, CppCassandraDriverTestIndex) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (1, 'one');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (2, 'two');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (3, 'three');"));
  LOG(INFO) << "Inserting one more to violate uniqueness";
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (-2, 'two');"));
  LOG(INFO) << "Creating index";

  auto s =
      session_.ExecuteQuery("create deferred index test_table_index_by_v_1 on test_table (v);");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  s = session_.ExecuteQuery(
      "create deferred unique index test_table_index_by_v_unq on test_table (v);");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  // Non deferred index.
  s = session_.ExecuteQuery("create unique index test_table_index_by_k on test_table (k);");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name_1(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v_1");
  const YBTableName index_table_name_2(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_k");
  const YBTableName index_table_name_unq(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v_unq");
  ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name_2,
      IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
      50ms /* max_wait */));

  auto res = client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name_unq, IndexPermissions::INDEX_PERM_NOT_USED, 50ms /* max_wait */);
  ASSERT_TRUE(!res.ok());
  ASSERT_TRUE(res.status().IsNotFound());

  ASSERT_OK(client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name_1,
      IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
      50ms /* max_wait */));
}

TEST_F_EX(CppCassandraDriverTest, TestCreateUniqueIndexFails, CppCassandraDriverTestIndexSlow) {
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"},
                              true));

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (1, 'one');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (2, 'two');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (3, 'three');"));
  ASSERT_OK(session_.ExecuteQuery(
      "insert into test_table (k, v) values (-2, 'two');"));
  LOG(INFO) << "Creating index";

  auto s = session_.ExecuteQuery(
      "create unique index test_table_index_by_v on test_table (v);");
  ASSERT_TRUE(CreateTableSuccessOrTimedOut(s));
  WARN_NOT_OK(s, "Create index command failed. " + s.ToString());

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  auto res = client_->WaitUntilIndexPermissionsAtLeast(
      table_name,
      index_table_name,
      IndexPermissions::INDEX_PERM_NOT_USED,
      50ms /* max_wait */);
  ASSERT_TRUE(!res.ok());
  ASSERT_TRUE(res.status().IsNotFound());

  ASSERT_OK(LoggedWaitFor(
      [this, index_table_name]() {
        Result<YBTableInfo> index_table_info = client_->GetYBTableInfo(index_table_name);
        return !index_table_info && index_table_info.status().IsNotFound();
      },
      10s, "waiting for index to be deleted"));

  LOG(INFO)
      << "Inserting more rows -- No collision checking for a failed index.";
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (-1, 'one');").ok();
      },
      10s, "insert after unique index creation failed."));
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (-3, 'three');").ok();
      },
      10s, "insert after unique index creation failed."));
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (4, 'four');").ok();
      },
      10s, "insert after unique index creation failed."));
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (-4, 'four');").ok();
      },
      10s, "insert after unique index creation failed."));
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (5, 'five');").ok();
      },
      10s, "insert after unique index creation failed."));
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        return session_.ExecuteQuery("insert into test_table (k, v) values (-5, 'five');").ok();
      },
      10s, "insert after unique index creation failed."));
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateUniqueIndexWithOnlineWriteFails,
    CppCassandraDriverTestIndexSlow) {
  DoTestCreateUniqueIndexWithOnlineWrites(this,
                                          /* delete_before_insert */ false);
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateUniqueIndexWithOnlineWriteSuccess,
    CppCassandraDriverTestIndexSlow) {
  DoTestCreateUniqueIndexWithOnlineWrites(this,
                                          /* delete_before_insert */ true);
}

void DoTestCreateUniqueIndexWithOnlineWrites(CppCassandraDriverTestIndex* test,
                                             bool delete_before_insert) {
  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace,
                                     "test_table_index_by_v");
  IndexInfoPB index_info_pb;
  YBTableInfo index_table_info;

  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&test->session_, "test.test_table", {"k", "v"},
                              {"(k)"}, true));

  LOG(INFO) << "Inserting three rows";
  ASSERT_OK(test->session_.ExecuteQuery(
      "insert into test_table (k, v) values (1, 'one');"));
  ASSERT_OK(test->session_.ExecuteQuery(
      "insert into test_table (k, v) values (2, 'two');"));
  ASSERT_OK(test->session_.ExecuteQuery(
      "insert into test_table (k, v) values (3, 'three');"));
  LOG(INFO) << "Creating index";

  bool create_index_failed = false;
  bool duplicate_insert_failed = false;
  {
    auto session2 = ASSERT_RESULT(test->EstablishSession());

    CassandraFuture create_index_future = session2.ExecuteGetFuture(
        "create unique index test_table_index_by_v on test_table (v);");

    auto session3 = ASSERT_RESULT(test->EstablishSession());
    ASSERT_RESULT(test->client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table_name, IndexPermissions::INDEX_PERM_WRITE_AND_DELETE));
    CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 90s,
                               CoarseMonoClock::Duration::max());
    if (delete_before_insert) {
      while (true) {
        auto res = session3
                       .ExecuteGetFuture(
                           "update test_table set v = 'foo' where  k = 2;")
                       .Wait();
        LOG(INFO) << "Got " << yb::ToString(res);
        if (res.ok()) {
          break;
        }
        waiter.Wait();
      }
      LOG(INFO) << "Successfully deleted the old value before inserting the "
                   "duplicate value";
    }
    int retries = 0;
    const int kMaxRetries = 12;
    Status res;
    while (++retries < kMaxRetries) {
      res = session3.ExecuteGetFuture(
                        "insert into test_table (k, v) values (-2, 'two');")
                    .Wait();
      LOG(INFO) << "Got " << yb::ToString(res);
      if (res.ok()) {
        break;
      }
      waiter.Wait();
    }
    duplicate_insert_failed = !res.ok();
    if (!duplicate_insert_failed) {
      LOG(INFO) << "Successfully inserted the duplicate value";
    } else {
      LOG(ERROR) << "Giving up on inserting the duplicate value after "
                 << kMaxRetries << " tries.";
    }

    LOG(INFO) << "Waited on the Create Index to finish. Status  = "
              << create_index_future.Wait();
  }

  Result<IndexPermissions> perm = test->client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

  create_index_failed = (!perm.ok() || *perm > IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  LOG(INFO) << "create_index_failed  = " << create_index_failed
            << ", duplicate_insert_failed = " << duplicate_insert_failed;

  auto main_table_size =
      ASSERT_RESULT(GetTableSize(&test->session_, "test_table"));
  auto index_table_size_result = GetTableSize(&test->session_, "test_table_index_by_v");

  if (!create_index_failed) {
    EXPECT_TRUE(index_table_size_result);
    EXPECT_EQ(main_table_size, *index_table_size_result);
  } else {
    LOG(INFO) << "create index failed. "
              << "main_table_size " << main_table_size << " is allowed to differ from "
              << "index_table_size_result " << index_table_size_result;
  }
  if (delete_before_insert) {
    // Expect both the create index, and the duplicate insert to succeed.
    ASSERT_TRUE(!create_index_failed && !duplicate_insert_failed);
  } else {
    // Expect exactly one of create index or the duplicate insert to succeed.
    ASSERT_TRUE((create_index_failed && !duplicate_insert_failed) ||
                (!create_index_failed && duplicate_insert_failed));
  }
}

TEST_F_EX(CppCassandraDriverTest, TestTableBackfillInChunks,
          CppCassandraDriverTestIndexMultipleChunks) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kFalse,
                         IncludeAllColumns::kTrue, UserEnforced::kFalse);
}

TEST_F_EX(
    CppCassandraDriverTest, TestTableBackfillWithLeaderMoves,
    CppCassandraDriverTestIndexMultipleChunksWithLeaderMoves) {
  TestBackfillIndexTable(
      this, PKOnlyIndex::kFalse, IsUnique::kFalse, IncludeAllColumns::kTrue, UserEnforced::kFalse);
}

TEST_F_EX(CppCassandraDriverTest, TestTableBackfillUniqueInChunks,
          CppCassandraDriverTestIndexMultipleChunks) {
  TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kTrue,
                         IncludeAllColumns::kTrue, UserEnforced::kFalse);
}

class CppCassandraDriverTestWithMasterFailover : public CppCassandraDriverTestIndex {
 private:
  virtual std::vector<std::string> ExtraMasterFlags() {
    auto flags = CppCassandraDriverTest::ExtraMasterFlags();
    flags.push_back("--TEST_slowdown_backfill_alter_table_rpcs_ms=200");
    flags.push_back("--vmodule=backfill_index=3,async_rpc_tasks=3");
    return flags;
  }

  virtual std::vector<std::string> ExtraTServerFlags() {
    auto flags = CppCassandraDriverTest::ExtraTServerFlags();
    flags.push_back("--TEST_backfill_paging_size=2");
    flags.push_back("--TEST_slowdown_backfill_by_ms=3000");
    return flags;
  }

  virtual int NumMasters() { return 3; }
};

TEST_F_EX(
    CppCassandraDriverTest, TestLogSpewDuringBackfill, CppCassandraDriverTestWithMasterFailover) {
  FLAGS_external_mini_cluster_max_log_bytes = 50_MB;
  // TestBackfillIndexTable(this, PKOnlyIndex::kFalse, IsUnique::kTrue, IncludeAllColumns::kTrue,
  //                        UserEnforced::kFalse, StepdownMasterLeader::kTrue);
  // Wait for Log spew
  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Inserting three rows";
  constexpr int kNumRows = 10;
  for (int i = 1; i < kNumRows; i++) {
    ASSERT_OK(session_.ExecuteQuery(Format("insert into test_table (k, v) values ($0, '$0');", i)));
  }
  // create a collision to fail the index.
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (0, '1');"));

  LOG(INFO) << "Creating index";
  auto create_future =
      session_.ExecuteGetFuture("create unique index test_table_index_by_v on test_table (v);");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");

  ASSERT_OK(WaitForBackfillSafeTimeOn(cluster_.get(), table_name));
  bool stepdown_master = true;
  if (stepdown_master) {
    const auto& leader_uuid = cluster_->GetLeaderMaster()->uuid();
    LOG(INFO) << "Stepping down master leader " << leader_uuid << " got "
              << admin_client_->MasterLeaderStepDown(leader_uuid);
  }

  auto res = client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_NOT_USED);
  LOG(INFO) << "Got " << yb::ToString(res);

  LOG(INFO) << "Waiting for log spew";
  SleepFor(MonoDelta::FromSeconds(60));
}

TEST_F_EX(CppCassandraDriverTest, TestIndexUpdateConcurrentTxn, CppCassandraDriverTestIndexSlow) {
  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");
  IndexInfoPB index_info_pb;
  YBTableInfo index_table_info;

  TestTable<cass_int32_t, string> table;
  ASSERT_OK(table.CreateTable(&session_, "test.test_table", {"k", "v"}, {"(k)"}, true));

  LOG(INFO) << "Inserting rows";
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (1, 'one');"));
  ASSERT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (2, 'two');"));

  LOG(INFO) << "Creating index";
  {
    auto session2 = ASSERT_RESULT(EstablishSession());

    CassandraFuture create_index_future =
        session2.ExecuteGetFuture("create index test_table_index_by_v on test_table (v);");

    auto session3 = ASSERT_RESULT(EstablishSession());
    ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table_name, IndexPermissions::INDEX_PERM_DELETE_ONLY));

    WARN_NOT_OK(session_.ExecuteQuery("insert into test_table (k, v) values (1, 'foo');"),
                "updating k = 1 failed.");
    WARN_NOT_OK(session3.ExecuteQuery("update test_table set v = 'bar' where  k = 2;"),
                "updating k =2 failed.");

    auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE));
    LOG(INFO) << "IndexPermissions is now " << IndexPermissions_Name(perm);
  }

  auto main_table_size = ASSERT_RESULT(GetTableSize(&session_, "test_table"));
  auto index_table_size = ASSERT_RESULT(GetTableSize(&session_, "test_table_index_by_v"));
  EXPECT_EQ(main_table_size, index_table_size);
}

TEST_F_EX(
    CppCassandraDriverTest, TestCreateMultipleIndex, CppCassandraDriverTestIndexSlowBackfill) {
  ASSERT_OK(session_.ExecuteQuery(
      "create table test_table (k1 int, k2 int, v text, PRIMARY KEY ((k1), k2)) "
      "with transactions = {'enabled' : true};"));

  constexpr int32_t kNumKeys = 1000;
  for (int i = 0; i < kNumKeys; i++) {
    ASSERT_OK(session_.ExecuteQuery(
        yb::Format("insert into test_table (k1, k2, v) values ($0, $0, 'v-$0');", i)));
  }
  LOG(INFO) << "Inserted " << kNumKeys << " rows.";

  std::atomic<int32_t> failed_cnt(0);
  std::atomic<int32_t> read_cnt(0);
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), &read_cnt, &failed_cnt] {
    SetFlagOnExit set_flag_on_exit(&stop);
    auto session = CHECK_RESULT(driver_->CreateSession());
    int32_t key = 0;
    constexpr int32_t kSleepTimeMs = 100;
    while (!stop) {
      key = (key + 1) % kNumKeys;
      SleepFor(MonoDelta::FromMilliseconds(kSleepTimeMs));
      read_cnt++;
      WARN_NOT_OK(
          session_.ExecuteQuery(
              yb::Format("select * from test_table where k1 = $0 and k2 = $0;", key)),
          yb::Format("Select failed for key = $0. failed count = $0", key, ++failed_cnt));
    }
  });
  LOG(INFO) << "Creating index";
  auto session = ASSERT_RESULT(EstablishSession());
  CassandraFuture create_index_future =
      session.ExecuteGetFuture("create index test_table_index_by_v on test_table (v);");

  constexpr auto kNamespace = "test";
  const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "test_table");
  const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_v");

  LOG(INFO) << "Creating index 2";
  auto session2 = ASSERT_RESULT(EstablishSession());

  IndexPermissions perm;
  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_DO_BACKFILL));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_DO_BACKFILL);
  LOG(INFO) << "Index table " << index_table_name.ToString()
            << " created to INDEX_PERM_DO_BACKFILL";

  // Launch a 2nd create-index while the first create index is still backfilling. We do this
  // from a different client session to prevent any client side serialization.
  CassandraFuture create_index_future2 =
      session2.ExecuteGetFuture("create index test_table_index_by_k2 on test_table (k2);");
  const YBTableName index_table_name2(YQL_DATABASE_CQL, kNamespace, "test_table_index_by_k2");

  const auto kMargin = 2;  // Account for time "wasted" due to RPC backoff delays.
  const auto kExpectedDuration = kMargin *
      kTimeMultiplier * static_cast<size_t>(ceil(1.0 * kNumKeys / kMaxBackfillRatePerSec)) * 1s;

  ASSERT_OK(WaitForBackfillSafeTimeOn(cluster_.get(), table_name));
  perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
      table_name, index_table_name, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
      CoarseMonoClock::now() + kExpectedDuration, 50ms));
  ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  LOG(INFO) << "Index table " << index_table_name.ToString()
            << " created to INDEX_PERM_READ_WRITE_AND_DELETE";

  perm = ASSERT_RESULT(client_->GetIndexPermissions(table_name, index_table_name2));
  if (perm != IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE) {
    ASSERT_OK(WaitForBackfillSafeTimeOn(cluster_.get(), table_name));
    perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name, index_table_name2, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
        CoarseMonoClock::now() + kExpectedDuration, 50ms));
    ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);
  }
  LOG(INFO) << "Index " << index_table_name2.ToString()
            << " created to INDEX_PERM_READ_WRITE_AND_DELETE";

  LOG(INFO) << "Waited on the Create Index to finish. Status  = " << create_index_future.Wait();
  LOG(INFO) << "Waited on the Create Index to finish. Status  = " << create_index_future2.Wait();

  thread_holder.Stop();
  LOG(INFO) << "Total failed read operations " << failed_cnt << " out of " << read_cnt;
  constexpr auto kFailurePctThreshold = 1;
  ASSERT_LE(failed_cnt.load(), read_cnt.load() * 0.01 * kFailurePctThreshold);
}

TEST_F_EX(CppCassandraDriverTest, TestDeleteAndCreateIndex, CppCassandraDriverTestIndex) {
  std::atomic<bool> stop(false);
  std::vector<std::thread> threads;

  typedef TestTable<int, int> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  WARN_NOT_OK(
      table.CreateTable(&session_, "test.key_value", {"key", "value"}, {"(key)"}, true, 60s),
      "Request timed out");

  std::thread write_thread([this, table, &stop] {
    CDSAttacher attacher;
    auto session = CHECK_RESULT(driver_->CreateSession());
    auto prepared = ASSERT_RESULT(table.PrepareInsert(&session, 10s));
    int32_t key = 0;
    constexpr int32_t kNumKeys = 10000;
    while (!stop) {
      key = (key + 1) % kNumKeys;
      auto statement = prepared.Bind();
      ColumnsType tuple(key, key);
      table.BindInsert(&statement, tuple);
      WARN_NOT_OK(session.Execute(statement), "Insert failed.");
    }
  });

  CoarseBackoffWaiter waiter(CoarseMonoClock::Now() + 90s, CoarseMonoClock::Duration::max());
  const int32_t kNumLoops = 10;
  vector<CassandraFuture> create_futures;
  create_futures.reserve(kNumLoops + 1);
  constexpr int kDelayMs = 50;

  vector<unique_ptr<CppCassandraDriver>> drivers;
  std::vector<std::string> hosts;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    hosts.push_back(cluster_->tablet_server(i)->bind_host());
  }
  for (size_t i = 0; i <= kNumLoops; i++) {
    drivers.emplace_back(new CppCassandraDriver(
        hosts, cluster_->tablet_server(0)->cql_rpc_port(), UsePartitionAwareRouting::kTrue));
  }

  for (int i = 0; i <= kNumLoops; i++) {
    const string curr_index_name = yb::Format("index_by_value_$0", i);
    LOG(INFO) << "Creating index " << curr_index_name;
    auto session = CHECK_RESULT(drivers[i]->CreateSession());
    create_futures.emplace_back(
        session.ExecuteGetFuture("create index " + curr_index_name + " on test.key_value (value)"));
    SleepFor(MonoDelta::FromMilliseconds(kDelayMs));
  }

  for (int i = 0; i <= kNumLoops; i++) {
    const string curr_index_name = yb::Format("index_by_value_$0", i);
    Status s = create_futures[i].Wait();
    WARN_NOT_OK(s, "Create index failed/TimedOut");
    EXPECT_TRUE(CreateTableSuccessOrTimedOut(s));
  }

  vector<CassandraFuture> delete_futures;
  delete_futures.reserve(kNumLoops);
  for (int i = 0; i <= kNumLoops; i++) {
    const string prev_index_name = yb::Format("index_by_value_$0", i - 1);
    const string curr_index_name = yb::Format("index_by_value_$0", i);

    constexpr auto kNamespace = "test";
    const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "key_value");
    const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, curr_index_name);
    auto perm = ASSERT_RESULT(client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE,
        CoarseMonoClock::Now() + 60s /* deadline */,
        CoarseDuration::max() /* max_wait */));
    ASSERT_EQ(perm, IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE);

    LOG(INFO) << "Waiting before deleting the index";
    waiter.Wait();
    LOG(INFO) << "Waiting done.";

    // Delete the existing index.
    if (i > 0) {
      auto session = CHECK_RESULT(drivers[i]->CreateSession());
      delete_futures.emplace_back(session.ExecuteGetFuture("drop index test." + prev_index_name));
      SleepFor(MonoDelta::FromMilliseconds(kDelayMs));
    }
  }

  for (int i = 0; i < kNumLoops; i++) {
    const string curr_index_name = yb::Format("index_by_value_$0", i);
    Status s = delete_futures[i].Wait();
    WARN_NOT_OK(s, "Drop index failed/TimedOut");
    EXPECT_TRUE(
        s.ok() || string::npos != s.ToUserMessage().find("Timed out waiting for Table Creation"));

    constexpr auto kNamespace = "test";
    const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "key_value");
    const YBTableName index_table_name(YQL_DATABASE_CQL, kNamespace, curr_index_name);
    auto res = client_->WaitUntilIndexPermissionsAtLeast(
        table_name,
        index_table_name,
        IndexPermissions::INDEX_PERM_NOT_USED,
        CoarseMonoClock::Now() + 60s /* deadline */,
        CoarseDuration::max() /* max_wait */);
    LOG(INFO) << "Got " << res << " for " << curr_index_name;
    ASSERT_TRUE(!res.ok());
    ASSERT_TRUE(res.status().IsNotFound());
  }

  stop.store(true, std::memory_order_release);
  write_thread.join();

  auto main_table_size = ASSERT_RESULT(GetTableSize(&session_, "test.key_value"));
  auto index_table_size =
      ASSERT_RESULT(GetTableSize(&session_, Format("test.index_by_value_$0", kNumLoops)));
  EXPECT_EQ(main_table_size, index_table_size);
}

TEST_F_EX(CppCassandraDriverTest, ConcurrentIndexUpdate, CppCassandraDriverTestIndex) {
  constexpr int kLoops = RegularBuildVsSanitizers(20, 10);
  constexpr int kKeys = 30;

  typedef TestTable<int, int> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.key_value",
                              {"key", "value"}, {"(key)"},
                              true));

  LOG(INFO) << "Creating index";
  ASSERT_OK(session_.ExecuteQuery("create index index_by_value on test.key_value (value)"));

  std::vector<CassandraFuture> futures;
  auto prepared = ASSERT_RESULT(table.PrepareInsert(&session_, 10s));
  for (int loop = 1; loop <= kLoops; ++loop) {
    for (int key = 0; key != kKeys; ++key) {
      auto statement = prepared.Bind();
      ColumnsType tuple(key, loop * 1000 + key);
      table.BindInsert(&statement, tuple);
      futures.push_back(session_.ExecuteGetFuture(statement));
    }
  }

  for (auto it = futures.begin(); it != futures.end();) {
    while (it != futures.end() && it->Ready()) {
      auto status = it->Wait();
      if (!status.ok()) {
        LOG(WARNING) << "Failure: " << status;
      }
      ++it;
    }
    for (;;) {
      auto result = session_.ExecuteWithResult("select * from index_by_value");
      if (!result.ok()) {
        LOG(WARNING) << "Read failed: " << result.status();
        continue;
      }
      auto iterator = result->CreateIterator();
      std::unordered_map<int, int> table_content;
      while (iterator.Next()) {
        auto row = iterator.Row();
        auto key = row.Value(0).As<int>();
        auto value = row.Value(1).As<int>();
        auto p = table_content.emplace(key, value);
        ASSERT_TRUE(p.second)
            << "Duplicate key: " << key << ", value: " << value
            << ", existing value: " << p.first->second;
      }
      break;
    }
  }

  for (;;) {
    constexpr int kBatchKey = 42;

    auto insert_status = session_.ExecuteQuery(Format(
        "BEGIN TRANSACTION "
        "INSERT INTO key_value (key, value) VALUES ($0, $1);"
        "INSERT INTO key_value (key, value) VALUES ($0, $2);"
        "END TRANSACTION;",
        kBatchKey, -100, -200));
    if (!insert_status.ok()) {
      LOG(INFO) << "Insert failed: " << insert_status;
      continue;
    }

    for (;;) {
      auto result = session_.ExecuteWithResult("select * from index_by_value");
      if (!result.ok()) {
        LOG(WARNING) << "Read failed: " << result.status();
        continue;
      }
      auto iterator = result->CreateIterator();
      int num_bad = 0;
      int num_good = 0;
      while (iterator.Next()) {
        auto row = iterator.Row();
        auto key = row.Value(0).As<int>();
        auto value = row.Value(1).As<int>();
        if (value < 0) {
          LOG(INFO) << "Key: " << key << ", value: " << value;
          ASSERT_EQ(key, kBatchKey);
          if (value == -200) {
            ++num_good;
          } else {
            ++num_bad;
          }
        }
      }
      ASSERT_EQ(num_good, 1);
      ASSERT_EQ(num_bad, 0);
      break;
    }
    break;
  }
}

YB_STRONGLY_TYPED_BOOL(RestartTS);

CQLMetrics TestPrepareWithTSRestart(const std::unique_ptr<ExternalMiniCluster>& cluster,
                                    CassandraSession* session,
                                    RestartTS restart_ts,
                                    const string& local_keyspace = string()) {
  CQLMetrics pre_metrics(*cluster);
  typedef TestTable<cass_bool_t, cass_int32_t, string, cass_int32_t, string> MyTable;
  MyTable table;
  CHECK_OK(table.CreateTable(
      session, "test.basic", {"b", "val", "key", "int_key", "str"}, {"key", "int_key"}));

  const MyTable::ColumnsTuple input(cass_true, 0xAABBCCDD, "key1test", 0xDEADBEAF, "mystr");
  {
    auto prepared = CHECK_RESULT(table.PrepareInsert(session, MonoDelta::kZero, local_keyspace));

    if (restart_ts) {
      LOG(INFO) << "Restart TS...";
      cluster->tablet_server(0)->Shutdown(); // Restart first TS.
      CHECK_OK(cluster->tablet_server(0)->Restart());
      LOG(INFO) << "Restart TS - DONE";
    }

    auto statement = prepared.Bind();
    // Prepared object can now be used to create new statement.
    table.Print("Execute prepared INSERT with INPUT", input);
    table.BindInsert(&statement, input);
    CHECK_OK(session->Execute(statement));
  }

  MyTable::ColumnsTuple output(cass_false, 0, "key1test", 0xDEADBEAF, "");
  table.SelectOneRow(session, &output);
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output);

  SleepFor(MonoDelta::FromSeconds(2)); // Let the metrics to be updated.
  const auto metrics = CQLMetrics(*cluster) - pre_metrics;
  LOG(INFO) << "DELTA Metrics: " << metrics;
  EXPECT_EQ(1, metrics.get("insert_count"));
  return CQLMetrics(metrics);
}

TEST_F(CppCassandraDriverTest, TestPrepare) {
  CQLMetrics metrics = TestPrepareWithTSRestart(cluster_, &session_, RestartTS::kFalse);
  EXPECT_EQ(2, metrics.get("use_count"));
}

TEST_F(CppCassandraDriverTest, TestPrepareWithLocalKeyspace) {
  CQLMetrics metrics = TestPrepareWithTSRestart(
      cluster_, &session_, RestartTS::kFalse, "ANY_KEYSPACE");
  EXPECT_EQ(2, metrics.get("use_count"));
}

class CppCassandraDriverTestWithoutUse : public CppCassandraDriverTest {
 protected:
  Status SetupSession(CassandraSession* session) override {
    LOG(INFO) << "Skipping 'USE test'";
    return CreateDefaultKeyspace(session);
  }
};

TEST_F_EX(CppCassandraDriverTest, TestPrepareWithRestart, CppCassandraDriverTestWithoutUse) {
  CQLMetrics metrics = TestPrepareWithTSRestart(cluster_, &session_, RestartTS::kTrue);
  EXPECT_EQ(0, metrics.get("use_count"));
}

TEST_F_EX(CppCassandraDriverTest,
          TestPrepareWithRestartAndLocalKeyspace,
          CppCassandraDriverTestWithoutUse) {
  CQLMetrics metrics = TestPrepareWithTSRestart(
      cluster_, &session_, RestartTS::kTrue, "ANY_KEYSPACE");
  EXPECT_EQ(0, metrics.get("use_count"));
}

template <typename... ColumnsTypes>
void TestTokenForTypes(
    CassandraSession* session,
    const vector<string>& columns,
    const vector<string>& keys,
    const tuple<ColumnsTypes...>& input_data,
    const tuple<ColumnsTypes...>& input_keys,
    const tuple<ColumnsTypes...>& input_empty,
    int64_t exp_token = 0) {
  typedef TestTable<ColumnsTypes...> MyTable;
  typedef typename MyTable::ColumnsTuple ColumnsTuple;

  MyTable table;
  ASSERT_OK(table.CreateTable(session, "test.basic", columns, keys));

  auto prepared = ASSERT_RESULT(table.PrepareInsert(session));
  auto statement = prepared.Bind();

  const ColumnsTuple input(input_data);
  table.Print("Execute prepared INSERT with INPUT", input);
  table.BindInsert(&statement, input);

  int64_t token = 0;
  bool token_available = cass_partition_aware_policy_get_yb_hash_code(
      statement.get(), &token);
  LOG(INFO) << "Got token: " << (token_available ? "OK" : "ERROR") << " token=" << token
            << " (0x" << std::hex << token << ")";
  ASSERT_TRUE(token_available);

  if (exp_token > 0) {
    ASSERT_EQ(exp_token, token);
  }

  ASSERT_OK(session->Execute(statement));

  ColumnsTuple output_by_key(input_keys);
  table.SelectOneRow(session, &output_by_key);
  table.Print("RESULT OUTPUT", output_by_key);
  LOG(INFO) << "Checking selected values...";
  ExpectEqualTuples(input, output_by_key);

  ColumnsTuple output = ASSERT_RESULT(table.SelectByToken(session, token));
  table.Print("RESULT OUTPUT", output);
  LOG(INFO) << "Checking selected by TOKEN values...";
  ExpectEqualTuples(input, output);
}

template <typename KeyType>
void TestTokenForType(
    CassandraSession* session, const KeyType& key, int64_t exp_token = 0) {
  typedef tuple<KeyType, cass_double_t> Tuple;
  TestTokenForTypes(session,
                    {"key", "value"}, // column names
                    {"(key)"}, // key names
                    Tuple(key, 0.56789), // data
                    Tuple(key, 0.), // only keys
                    Tuple((KeyType()), 0.), // empty
                    exp_token);
}

TEST_F(CppCassandraDriverTest, TestTokenForText) {
  TestTokenForType<string>(&session_, "test", 0x8753000000000000);
}

TEST_F(CppCassandraDriverTest, TestTokenForInt) {
  TestTokenForType<int32_t>(&session_, 0xDEADBEAF);
}

TEST_F(CppCassandraDriverTest, TestTokenForBigInt) {
  TestTokenForType<int64_t>(&session_, 0xDEADBEAFDEADBEAFULL);
}

TEST_F(CppCassandraDriverTest, TestTokenForBoolean) {
  TestTokenForType<cass_bool_t>(&session_, cass_true);
}

TEST_F(CppCassandraDriverTest, TestTokenForFloat) {
  TestTokenForType<cass_float_t>(&session_, 0.123f);
}

TEST_F(CppCassandraDriverTest, TestTokenForDouble) {
  TestTokenForType<cass_double_t>(&session_, 0.12345);
}

TEST_F(CppCassandraDriverTest, TestTokenForDoubleKey) {
  typedef tuple<string, cass_int32_t, cass_double_t> Tuple;
  TestTokenForTypes(&session_,
                    {"key", "int_key", "value"}, // column names
                    {"(key", "int_key)"}, // key names
                    Tuple("test", 0xDEADBEAF, 0.123), // data
                    Tuple("test", 0xDEADBEAF, 0.), // only keys
                    Tuple("", 0, 0.)); // empty
}

//------------------------------------------------------------------------------
class CppCassandraDriverTestNoPartitionBgRefresh : public CppCassandraDriverTest {
 protected:
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTest::ExtraMasterFlags();
    flags.push_back("--partitions_vtable_cache_refresh_secs=0");
    return flags;
  }
};

TEST_F_EX(CppCassandraDriverTest, TestInsertLocality, CppCassandraDriverTestNoPartitionBgRefresh) {
  typedef TestTable<string, string> MyTable;
  typedef typename MyTable::ColumnsTuple ColumnsTuple;

  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.basic", {"id", "data"}, {"(id)"}));

  LOG(INFO) << "Wait 5 sec to refresh metadata in driver by time";
  SleepFor(MonoDelta::FromMicroseconds(5*1000000));

  IOMetrics pre_metrics(*cluster_);

  auto prepared = ASSERT_RESULT(table.PrepareInsert(&session_));
  const int total_keys = 100;
  ColumnsTuple input("", "test_value");

  for (int i = 0; i < total_keys; ++i) {
    get<0>(input) = Substitute("key_$0", i);

    // Prepared object can now be used to create new statement.
    auto statement = prepared.Bind();
    table.BindInsert(&statement, input);
    ASSERT_OK(session_.Execute(statement));
  }

  IOMetrics post_metrics(*cluster_);
  const auto delta_metrics = post_metrics - pre_metrics;
  LOG(INFO) << "DELTA Metrics: " << delta_metrics;

  // Expect minimum 70% of all requests to be local.
  ASSERT_GT(delta_metrics.get("local_write")*10, total_keys*7);
}

class CppCassandraDriverLowSoftLimitTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--memory_limit_soft_percentage=0"s,
            "--throttle_cql_calls_on_soft_memory_limit=false"s};
  }
};

TEST_F_EX(CppCassandraDriverTest, BatchWriteDuringSoftMemoryLimit,
          CppCassandraDriverLowSoftLimitTest) {
  FLAGS_external_mini_cluster_max_log_bytes = 512_MB;

  constexpr int kBatchSize = 500;
  constexpr int kWriters = 4;
  constexpr int kNumMetrics = 5;

  typedef TestTable<std::string, int64_t, std::string> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, "test.batch_ts_metrics_raw", {"metric_id", "ts", "value"},
      {"(metric_id, ts)"}));

  TestThreadHolder thread_holder;
  std::array<std::atomic<int>, kNumMetrics> metric_ts;
  std::atomic<int> total_writes(0);
  for (int i = 0; i != kNumMetrics; ++i) {
    metric_ts[i].store(0);
  }

  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &table, &metric_ts, &total_writes] {
      SetFlagOnExit set_flag_on_exit(&stop);
      auto session = ASSERT_RESULT(EstablishSession());
      std::vector<CassandraFuture> futures;
      while (!stop.load()) {
        CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
        auto prepared = table.PrepareInsert(&session);
        if (!prepared.ok()) {
          // Prepare could be failed because cluster has heavy load.
          // It is ok to just retry in this case, because we expect total number of writes.
          continue;
        }
        int metric_idx = RandomUniformInt(1, kNumMetrics);
        auto metric = "metric_" + std::to_string(metric_idx);
        int ts = metric_ts[metric_idx - 1].fetch_add(kBatchSize);
        for (int i = 0; i != kBatchSize; ++i) {
          ColumnsType tuple(metric, ts, "value_" + std::to_string(ts));
          auto statement = prepared->Bind();
          table.BindInsert(&statement, tuple);
          batch.Add(&statement);
          ++ts;
        }
        futures.push_back(session.SubmitBatch(batch));
        ++total_writes;
      }
    });
  }

  thread_holder.WaitAndStop(30s);
  auto total_writes_value = total_writes.load();
  LOG(INFO) << "Total writes: " << total_writes_value;
#ifndef NDEBUG
  auto min_total_writes = RegularBuildVsDebugVsSanitizers(750, 500, 50);
#else
  auto min_total_writes = 1500;
#endif
  ASSERT_GE(total_writes_value, min_total_writes);
}

class CppCassandraDriverBackpressureTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--tablet_server_svc_queue_length=10"s, "--max_time_in_queue_ms=-1"s};
  }
};

TEST_F_EX(CppCassandraDriverTest, LocalCallBackpressure, CppCassandraDriverBackpressureTest) {
  constexpr int kBatchSize = 30;
  constexpr int kNumBatches = 300;

  typedef TestTable<int64_t, int64_t> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.key_value", {"key", "value"}, {"(key)"}));

  std::vector<CassandraFuture> futures;

  for (int batch_idx = 0; batch_idx != kNumBatches; ++batch_idx) {
    CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
    auto prepared = table.PrepareInsert(&session_);
    if (!prepared.ok()) {
      // Prepare could be failed because cluster has heavy load.
      // It is ok to just retry in this case, because we check that process did not crash.
      continue;
    }
    for (int i = 0; i != kBatchSize; ++i) {
      ColumnsType tuple(batch_idx * kBatchSize + i, -1);
      auto statement = prepared->Bind();
      table.BindInsert(&statement, tuple);
      batch.Add(&statement);
    }
    futures.push_back(session_.SubmitBatch(batch));
  }

  for (auto& future : futures) {
    WARN_NOT_OK(future.Wait(), "Write failed");
  }
}

class CppCassandraDriverTransactionalWriteTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--TEST_transaction_inject_flushed_delay_ms=10"s};
  }
};

TEST_F_EX(CppCassandraDriverTest, TransactionalWrite, CppCassandraDriverTransactionalWriteTest) {
  const std::string kTableName = "test.key_value";
  typedef TestTable<int32_t, int32_t> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(
      &session_, kTableName, {"key", "value"}, {"(key)"}, true /* transactional */));

  constexpr int kIterations = 20;
  auto prepared = ASSERT_RESULT(session_.Prepare(Format(
      "BEGIN TRANSACTION"
      "  INSERT INTO $0 (key, value) VALUES (?, ?);"
      "  INSERT INTO $0 (key, value) VALUES (?, ?);"
      "END TRANSACTION;", kTableName)));
  for (int i = 1; i <= kIterations; ++i) {
    auto statement = prepared.Bind();
    statement.Bind(0, i);
    statement.Bind(1, i * 3);
    statement.Bind(2, -i);
    statement.Bind(3, i * -4);
    ASSERT_OK(session_.Execute(statement));
  }
}

class CppCassandraDriverTestThreeMasters : public CppCassandraDriverTestNoPartitionBgRefresh {
 private:
  int NumMasters() override {
    return 3;
  }

  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTestNoPartitionBgRefresh::ExtraMasterFlags();
  // Just want to test the cache, so turn off automatic updating for this vtable.
    flags.push_back("--generate_partitions_vtable_on_changes=false");
    return flags;
  }
};

TEST_F_EX(CppCassandraDriverTest, ManyTables, CppCassandraDriverTestThreeMasters) {
  FLAGS_external_mini_cluster_max_log_bytes = 512_MB;

  constexpr int kThreads = RegularBuildVsSanitizers(5, 2);
  constexpr int kTables = RegularBuildVsSanitizers(15, 5);
  constexpr int kReads = 20;

  const std::string kTableNameFormat = "test.key_value_$0_$1";
  typedef TestTable<int32_t, int32_t> MyTable;

  TestThreadHolder thread_holder;
  std::atomic<int> tables(0);

  for (int i = 0; i != kThreads; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), thread = i, &kTableNameFormat, &tables] {
          SetFlagOnExit set_flag_on_exit(&stop);
          auto session = ASSERT_RESULT(EstablishSession());
          int idx = 0;
          while (!stop.load(std::memory_order_acquire)) {
            MyTable table;
            auto status = table.CreateTable(
                &session, Format(kTableNameFormat, thread, idx), {"key", "value"}, {"(key)"});
            if (status.ok()) {
              LOG(INFO) << "Created table " << thread << ", " << idx;
              // We need at least kTables tables.
              if (tables.fetch_add(1, std::memory_order_acq_rel) >= kTables) {
                break;
              }
            } else {
              LOG(INFO) << "Failed to create table " << thread << ", " << idx << ": " << status;
            }
            ++idx;
          }
        });
  }

  thread_holder.WaitAndStop(180s);

  ASSERT_GE(tables.load(std::memory_order_acquire), kTables);

  CassandraStatement statement("SELECT * FROM system.partitions");
  std::vector<MonoDelta> read_times;
  read_times.reserve(kReads);
  int i = 0;
  for (;;) {
    auto start = MonoTime::Now();
    auto result = session_.ExecuteWithResult(statement);
    auto finish = MonoTime::Now();
    if (!result.ok()) {
      LOG(INFO) << "Read failed: " << result.status();
      continue;
    }
    read_times.push_back(finish - start);
    ++i;
    if (i == kReads) {
      LogResult(*result);
      break;
    }
  }

  LOG(INFO) << "Read times: " << AsString(read_times);
  std::sort(read_times.begin(), read_times.end());

  if (!IsSanitizer()) {
    ASSERT_LE(read_times.front() * 2, read_times.back()); // Check that cache works
  }
}

class CppCassandraDriverTestPartitionsVtableCache : public CppCassandraDriverTest {
 public:
  static constexpr int kCacheRefreshSecs = 30;

  vector<string> ResultsToList(const CassandraResult& result) {
    vector<string> out;
    auto iterator = result.CreateIterator();
    while (iterator.Next()) {
      auto row = iterator.Row();
      auto row_iterator = row.CreateIterator();
      while (row_iterator.Next()) {
        out.push_back(row_iterator.Value().ToString());
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  }

  Status AddTable() {
    auto session = VERIFY_RESULT(EstablishSession());
    TestTable<int32_t, int32_t>  table;
    return table.CreateTable(
        &session, Format("test.key_value_$0", ++table_idx_), {"key", "value"}, {"(key)"});
  }

  Status DropTable() {
    auto session = VERIFY_RESULT(EstablishSession());
    CassandraStatement statement(Format("DROP TABLE test.key_value_$0", table_idx_--));
    return ResultToStatus(session.ExecuteWithResult(statement));
  }

 private:
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTest::ExtraMasterFlags();
    flags.push_back(Substitute("--partitions_vtable_cache_refresh_secs=$0", kCacheRefreshSecs));
    flags.push_back(Substitute("--generate_partitions_vtable_on_changes=$0", false));
    flags.push_back(Substitute(
        "--TEST_catalog_manager_check_yql_partitions_exist_for_is_create_table_done=$0",
        false));
    return flags;
  }

  int table_idx_ = 0;
};

TEST_F_EX(CppCassandraDriverTest,
          YQLPartitionsVtableCacheRefresh,
          CppCassandraDriverTestPartitionsVtableCache) {
  // Get the initial system.partitions and store the result.
  CassandraStatement statement("SELECT * FROM system.partitions");
  auto old_result = ASSERT_RESULT(session_.ExecuteWithResult(statement));
  auto old_results = ResultsToList(old_result);

  // Add a table to update system.partitions.
  ASSERT_OK(AddTable());

  // Since we don't know when the bg task started, let's wait for a cache update.
  ASSERT_OK(LoggedWaitFor(
      [this, &old_results]() {
        CassandraStatement statement("SELECT * FROM system.partitions");
        auto new_result = session_.ExecuteWithResult(statement);
        if (!new_result.ok()) {
          return false;
        }
        auto new_results = ResultsToList(*new_result);
        if (old_results != new_results) {
          // Update the old_results.
          old_results = new_results;
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(kCacheRefreshSecs), "Waiting for cache to refresh"));

  // We are now just after a cache update, so we should expect that we only get the cached value
  // for the next kCacheRefreshSecs seconds.

  // Add a table to update system.partitions again. Don't invalidate the cache on this create.
  ASSERT_OK(cluster_->SetFlagOnMasters("invalidate_yql_partitions_cache_on_create_table", "false"));
  ASSERT_OK(AddTable());

  // Check that we still get the same cached version as the bg task has not run yet.
  auto new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_EQ(old_results, new_results);

  // Wait for the cache to update.
  SleepFor(MonoDelta::FromSeconds(kCacheRefreshSecs));
  // Verify that we get the new cached value.
  new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_NE(old_results, new_results);
  old_results = new_results;

  // Add another table to update system.partitions again, but this time enable the gflag to
  // invalidate the cache.
  ASSERT_OK(cluster_->SetFlagOnMasters("invalidate_yql_partitions_cache_on_create_table", "true"));
  ASSERT_OK(AddTable());

  // The cache was invalidated, so the new results should be updated.
  new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_NE(old_results, new_results);
  old_results = new_results;

  // Test dropping a table as well.
  ASSERT_OK(DropTable());
  // Should still get the old cached values.
  new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_EQ(old_results, new_results);

  // Wait for the cache to update, then verify that we get the new cached value.
  SleepFor(MonoDelta::FromSeconds(kCacheRefreshSecs));
  new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_NE(old_results, new_results);
}

class CppCassandraDriverTestPartitionsVtableCacheUpdateOnChanges :
    public CppCassandraDriverTestPartitionsVtableCache {
  std::vector<std::string> ExtraMasterFlags() override {
    auto flags = CppCassandraDriverTest::ExtraMasterFlags();
    // Test for generating system.partitions as changes occur, rather than via a bg task.
    flags.push_back(Substitute("--partitions_vtable_cache_refresh_secs=$0", 0));
    flags.push_back(Substitute("--generate_partitions_vtable_on_changes=$0", true));
    flags.push_back(Substitute(
        "--TEST_catalog_manager_check_yql_partitions_exist_for_is_create_table_done=$0",
        true));
    return flags;
  }
};

TEST_F_EX(CppCassandraDriverTest,
          YQLPartitionsVtableCacheUpdateOnChanges,
          CppCassandraDriverTestPartitionsVtableCacheUpdateOnChanges) {
  // Get the initial system.partitions and store the result.
  CassandraStatement statement("SELECT * FROM system.partitions");
  auto old_result = ASSERT_RESULT(session_.ExecuteWithResult(statement));
  auto old_results = ResultsToList(old_result);

  // Add a table to update system.partitions.
  ASSERT_OK(AddTable());

  // Verify that the table is in the cache.
  auto new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_NE(old_results, new_results);
  old_results = new_results;

  // Test dropping a table as well.
  ASSERT_OK(DropTable());
  // Cache should again be automatically updated.
  new_results = ResultsToList(ASSERT_RESULT(session_.ExecuteWithResult(statement)));
  ASSERT_NE(old_results, new_results);
}

class CppCassandraDriverRejectionTest : public CppCassandraDriverTest {
 public:
  std::vector<std::string> ExtraTServerFlags() override {
    return {"--TEST_write_rejection_percentage=15"s,
            "--linear_backoff_ms=10"};
  }
};

TEST_F_EX(CppCassandraDriverTest, Rejection, CppCassandraDriverRejectionTest) {
  constexpr int kBatchSize = 50;
  constexpr int kWriters = 21;

  typedef TestTable<int64_t, int64_t> MyTable;
  typedef MyTable::ColumnsTuple ColumnsType;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, "test.key_value", {"key", "value"}, {"(key)"}));

  TestThreadHolder thread_holder;
  std::atomic<int64_t> key(0);
  std::atomic<int> pending_writes(0);
  std::atomic<int> max_pending_writes(0);

  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &table, &key, &pending_writes,
         &max_pending_writes] {
      auto session = ASSERT_RESULT(EstablishSession());
      CassandraPrepared prepared;
      ASSERT_OK(WaitFor([&table, &session, &prepared] {
        auto prepared_result = table.PrepareInsert(&session);
        if (!prepared_result.ok()) {
          // Prepare could be failed because cluster has heavy load.
          // It is ok to just retry in this case, because we expect total number of writes.
          LOG(INFO) << "Prepare failed: " << prepared_result.status();
          return false;
        }
        prepared = std::move(*prepared_result);
        return true;
      }, kCassandraTimeOut * 5, "Prepare statement"));
      while (!stop.load()) {
        CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
        for (int i = 0; i != kBatchSize; ++i) {
          auto current_key = key++;
          ColumnsType tuple(current_key, -current_key);
          auto statement = prepared.Bind();
          table.BindInsert(&statement, tuple);
          batch.Add(&statement);
        }
        auto future = session.SubmitBatch(batch);
        auto status = future.WaitFor(kCassandraTimeOut / 2);
        if (status.IsTimedOut()) {
          auto pw = ++pending_writes;
          auto mpw = max_pending_writes.load();
          while (pw > mpw) {
            if (max_pending_writes.compare_exchange_weak(mpw, pw)) {
              break;
            }
          }
          auto wait_status = future.Wait();
          ASSERT_TRUE(wait_status.ok() || wait_status.IsTimedOut()) << wait_status;
          --pending_writes;
        } else {
          ASSERT_OK(status);
        }
      }
    });
  }

  thread_holder.WaitAndStop(30s);
  LOG(INFO) << "Max pending writes: " << max_pending_writes.load();
  // Assert that we don't have too many pending writers.
  ASSERT_LE(max_pending_writes.load(), kWriters / 3);
}

TEST_F(CppCassandraDriverTest, BigQueryExpr) {
  const std::string kTableName = "test.key_value";
  typedef TestTable<std::string> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, kTableName, {"key"}, {"(key)"}));

  constexpr size_t kRows = 400;
  constexpr size_t kValueSize = RegularBuildVsSanitizers(256_KB, 4_KB);

  auto prepared = ASSERT_RESULT(session_.Prepare(
      Format("INSERT INTO $0 (key) VALUES (?);", kTableName)));

  for (int32_t i = 0; i != kRows; ++i) {
    auto statement = prepared.Bind();
    statement.Bind(0, RandomHumanReadableString(kValueSize));
    ASSERT_OK(session_.Execute(statement));
  }

  auto start = MonoTime::Now();
  auto result = ASSERT_RESULT(session_.FetchValue<std::string>(Format(
      "SELECT MAX(key) FROM $0", kTableName)));
  auto finish = MonoTime::Now();
  LOG(INFO) << "Time: " << finish - start;

  LOG(INFO) << "Result: " << result;
}

class CppCassandraDriverSmallSoftLimitTest : public CppCassandraDriverTest {
 public:
  std::vector <std::string> ExtraTServerFlags() override {
    return {
        Format("--memory_limit_hard_bytes=$0", 100_MB),
        "--memory_limit_soft_percentage=10"
    };
  }
};

TEST_F_EX(CppCassandraDriverTest, Throttle, CppCassandraDriverSmallSoftLimitTest) {
  const std::string kTableName = "test.key_value";
  typedef TestTable<std::string> MyTable;
  MyTable table;
  ASSERT_OK(table.CreateTable(&session_, kTableName, {"key"}, {"(key)"}));

  constexpr size_t kValueSize = 1_KB;

  CassandraPrepared prepared;
  for (;;) {
    auto temp_prepared = session_.Prepare(
        Format("INSERT INTO $0 (key) VALUES (?);", kTableName));
    if (temp_prepared.ok()) {
      prepared = std::move(*temp_prepared);
      break;
    }
    LOG(INFO) << "Prepare failure: " << temp_prepared.status();
  }

  bool has_failure = false;

  auto deadline = CoarseMonoClock::now() + 60s;
  while (CoarseMonoClock::now() < deadline) {
    auto statement = prepared.Bind();
    statement.Bind(0, RandomHumanReadableString(kValueSize));
    auto status = session_.Execute(statement);
    if (!status.ok()) {
      ASSERT_TRUE(status.IsServiceUnavailable() || status.IsTimedOut()) << status;
      has_failure = true;
      break;
    }
  }

  ASSERT_TRUE(RegularBuildVsSanitizers(has_failure, true));
}

}  // namespace yb
