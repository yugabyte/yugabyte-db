// Copyright (c) YugabyteDB, Inc.
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

#include <csignal>
#include <set>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "yb/cdc/cdc_consumer.pb.h"

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

#include "yb/master/master_defaults.h"

#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/subprocess.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(TEST_pg_auth_key);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_yb_enable_replication_slot_consumption);

using namespace std::literals;

namespace yb::cdc {

namespace {

const auto kTimeout = 60s * kTimeMultiplier;
const std::string kNilUuid = "00000000-0000-0000-0000-000000000000";

// The attribute numbers of the columns of a table made by CreateTestTable.
constexpr int kIdAttnum = 1;
constexpr int kValueAttnum = 2;

// A change in the CDC table, as the tests look at it.
struct CdcTableChange {
  std::string op;
  bool single_shard;
  std::optional<CdcTableImagePB> before;
  std::optional<CdcTableImagePB> after;
};

// An image from the hex of its bytea, or nullopt for the "-" that stands for NULL.
Result<std::optional<CdcTableImagePB>> ParseImage(const std::string& hex) {
  if (hex == "-") {
    return std::nullopt;
  }
  CdcTableImagePB image;
  SCHECK(image.ParseFromString(a2b_hex(hex)), Corruption, "Not a CdcTableImagePB: $0", hex);
  return image;
}

std::vector<std::string> SplitTrimmed(const std::string& text, char separator) {
  std::vector<std::string> parts;
  boost::split(parts, text, boost::is_any_of(std::string(1, separator)));
  for (auto& part : parts) {
    boost::trim(part);
  }
  return parts;
}

// The changes of the CDC table that match the filter, oldest first.
Result<std::vector<CdcTableChange>> FetchChanges(
    pgwrapper::PGConn& conn, const std::string& filter = "true") {
  const auto rows = VERIFY_RESULT(conn.FetchAllAsString(Format(
      "SELECT op, CASE WHEN transaction_id = '$0' THEN 'single' ELSE 'txn' END, "
      "COALESCE(encode(before, 'hex'), '-'), COALESCE(encode(after, 'hex'), '-') "
      "FROM $1 WHERE $2 ORDER BY commit_time, record_time, write_id",
      kNilUuid, master::kPgYbCdcChangesTableName, filter)));
  std::vector<CdcTableChange> changes;
  if (rows.empty()) {
    return changes;
  }
  for (const auto& row : SplitTrimmed(rows, ';')) {
    const auto columns = SplitTrimmed(row, ',');
    SCHECK_EQ(columns.size(), 4U, Corruption, Format("Not a change: $0", row));
    changes.push_back(CdcTableChange{
        .op = columns[0],
        .single_shard = columns[1] == "single",
        .before = VERIFY_RESULT(ParseImage(columns[2])),
        .after = VERIFY_RESULT(ParseImage(columns[3]))});
  }
  return changes;
}

const CdcTableColumnPB* FindColumn(const CdcTableImagePB& image, int attnum) {
  for (const auto& column : image.columns()) {
    if (column.attnum() == attnum) {
      return &column;
    }
  }
  return nullptr;
}

// The int4 id of a change, from its after image or else its before image.
int32_t IdOf(const CdcTableChange& change) {
  const auto& image = change.after ? *change.after : *change.before;
  return FindColumn(image, kIdAttnum)->value().int32_value();
}

// The text of the value column of an image, "-" for a missing image.
std::string ValueOf(const std::optional<CdcTableImagePB>& image) {
  return image ? FindColumn(*image, kValueAttnum)->value().string_value() : "-";
}

}  // namespace

// Tests that the changes of the published tables of a database end up in its pg_yb_cdc_changes
// table once a replication slot exists.
class CDCSDKCdcTableTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_enable_cdc_table) = true;
    // With packed rows the CDC service reports the update of a row as an insert without a before
    // image, which is not what the table is meant to hold.
    // TODO: Find out why, and remove this.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = true;
    // Tablet splits are not handled by the CDC table pollers yet.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // The master creates the CDC table through the postgres of some tserver, and only one
    // tserver of the mini cluster runs postgres. With the same auth key, every tserver can
    // connect to that one.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pg_auth_key) =
        RandomUniformInt<uint64_t>(1, std::numeric_limits<uint64_t>::max());
  }

  Status SetUpCluster() {
    return SetUpWithParams(
        3 /* replication_factor */, 1 /* num_masters */, false /* colocated */,
        false /* cdc_populate_safepoint_record */, true /* set_pgsql_proxy_bind_address */);
  }

  // Creates table (id int primary key, v text) with full before images.
  Status CreateTestTable(pgwrapper::PGConn& conn, const std::string& name, int num_tablets = 3) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, v TEXT) SPLIT INTO $1 TABLETS", name, num_tablets));
    return conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY FULL", name);
  }

  // The filter on pg_yb_cdc_changes that selects the changes of the given table.
  Result<std::string> TableFilter(const std::string& table_name) {
    auto table = VERIFY_RESULT(GetTable(&test_cluster_, test_namespace_name, table_name));
    return Format("replace(table_id::text, '-', '') = '$0'", table.table_id());
  }

  Result<int64_t> CountChanges(pgwrapper::PGConn& conn, const std::string& filter = "true") {
    return conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM $0 WHERE $1", master::kPgYbCdcChangesTableName, filter));
  }

  Status WaitForChanges(
      pgwrapper::PGConn& conn, int64_t expected, const std::string& filter = "true") {
    int64_t count = 0;
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          count = VERIFY_RESULT(CountChanges(conn, filter));
          return count >= expected;
        },
        kTimeout,
        Format("Wait for $0 changes matching [$1] in the CDC table", expected, filter));
  }

  // Creates a HYBRID_TIME slot with the test_decoding plugin, which prints a change as text.
  Status CreateTextSlot(pgwrapper::PGConn& conn, const std::string& slot_name) {
    RETURN_NOT_OK(conn.FetchFormat(
        "SELECT * FROM pg_create_logical_replication_slot('$0', 'test_decoding', false, false, "
        "'HYBRID_TIME')", slot_name));
    return Status::OK();
  }

  // Starts pg_recvlogical on the slot with a walsender that reads the CDC table instead of calling
  // the CDC service, which the pollers have taken over.
  Result<std::unique_ptr<Subprocess>> StartRecvlogical(
      const std::string& slot_name, const std::string& out_file,
      const std::vector<std::string>& extra_args = {}) {
    const auto& pg = test_cluster_.pg_host_port_;
    const auto pg_recvlogical = GetPgToolPath("pg_recvlogical");
    std::vector<std::string> args{
        pg_recvlogical, Format("--dbname=$0", test_namespace_name), "--username=yugabyte",
        Format("--host=$0", pg.host()), Format("--port=$0", pg.port()),
        Format("--slot=$0", slot_name), "--option=include-xids=0",
        Format("--file=$0", out_file)};
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    args.push_back("--start");
    auto proc = std::make_unique<Subprocess>(pg_recvlogical, args);
    proc->SetEnv("PGOPTIONS", "-c yb_use_table_based_cdc=on");
    RETURN_NOT_OK(proc->Start());
    return proc;
  }

  // Waits until the output file has at least as much as expected, then returns what it has.
  Result<std::string> WaitForOutput(const std::string& out_file, const std::string& expected) {
    std::string content;
    auto status = LoggedWaitFor(
        [&]() -> Result<bool> {
          // The file does not exist until pg_recvlogical has connected.
          if (!Env::Default()->FileExists(out_file)) {
            return false;
          }
          faststring data;
          RETURN_NOT_OK(ReadFileToString(Env::Default(), out_file, &data));
          content = data.ToString();
          return content.size() >= expected.size();
        },
        kTimeout, "Wait for the walsender output");
    // The caller compares the output with the expectation, which says more than a timeout.
    if (!status.ok() && content.empty()) {
      return status;
    }
    return content;
  }
};

TEST_F(CDCSDKCdcTableTest, TablePublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));

  auto stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // Creating the slot created the CDC table, which is not part of the stream.
  auto cdc_table = ASSERT_RESULT(
      GetTable(&test_cluster_, test_namespace_name, master::kPgYbCdcChangesTableName));
  auto stream = ASSERT_RESULT(GetCDCStream(stream_id));
  for (const auto& table_id : stream.stream().table_id()) {
    ASSERT_NE(table_id, cdc_table.table_id());
  }

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (1, 'x')"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'b'), (3, 'c')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'a2' WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 3"));

  const auto t1_filter = ASSERT_RESULT(TableFilter("t1"));
  ASSERT_OK(WaitForChanges(conn, 5, t1_filter));

  // id, op, value before, value after, whether the row has a transaction id. A before image
  // of an insert and an after image of a delete do not exist.
  auto changes = ASSERT_RESULT(FetchChanges(conn, t1_filter));
  std::stable_sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
    return IdOf(a) < IdOf(b);
  });
  std::vector<std::string> rows;
  for (const auto& change : changes) {
    rows.push_back(Format(
        "$0, $1, $2, $3, $4", IdOf(change), change.op, ValueOf(change.before),
        ValueOf(change.after), change.single_shard ? "single" : "txn"));
  }
  ASSERT_EQ(
      boost::join(rows, "; "),
      "1, I, -, a, single; 1, U, a, a2, txn; 2, I, -, b, txn; 3, I, -, c, txn; "
      "3, D, c, -, single");

  // The three changes of the transaction share its id and commit time.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(DISTINCT (transaction_id, commit_time)) FROM $0 "
          "WHERE $1 AND transaction_id != '$2'",
          master::kPgYbCdcChangesTableName, t1_filter, kNilUuid))),
      1);

  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, "bucket_id != yb_hash_code(row_ybctid) % 256")), 0);

  // An image has the type and the undecoded value of each column, by attribute number. 23 and 25
  // are int4 and text.
  const auto image_of = [&](int32_t id) -> const CdcTableImagePB& {
    for (const auto& change : changes) {
      if (change.op == "I" && IdOf(change) == id) {
        return *change.after;
      }
    }
    LOG(FATAL) << "No insert of id " << id;
    __builtin_unreachable();
  };
  ASSERT_EQ(
      image_of(1).ShortDebugString(),
      R"(columns { attnum: 1 type_oid: 23 value { int32_value: 1 } } )"
      R"(columns { attnum: 2 type_oid: 25 value { string_value: "a" } })");

  // A NULL column is present, without a value. A value that is the default of its protobuf
  // field is not left out.
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (0, NULL)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (4, '')"));
  ASSERT_OK(WaitForChanges(conn, 7, t1_filter));
  changes = ASSERT_RESULT(FetchChanges(conn, t1_filter));
  ASSERT_EQ(
      image_of(0).ShortDebugString(),
      R"(columns { attnum: 1 type_oid: 23 value { int32_value: 0 } } )"
      R"(columns { attnum: 2 type_oid: 25 })");
  ASSERT_EQ(
      image_of(4).ShortDebugString(),
      R"(columns { attnum: 1 type_oid: 23 value { int32_value: 4 } } )"
      R"(columns { attnum: 2 type_oid: 25 value { string_value: "" } })");

  // t2 is not published.
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, ASSERT_RESULT(TableFilter("t2")))), 0);
}

TEST_F(CDCSDKCdcTableTest, AllTablesPublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));

  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // The CDC table must not publish its own rows.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM pg_publication_tables WHERE tablename = '$0'",
          master::kPgYbCdcChangesTableName))),
      0);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 SELECT i, 'x' FROM generate_series(1, 10) i"));
  ASSERT_OK(WaitForChanges(conn, 20, ASSERT_RESULT(TableFilter("t1"))));
  ASSERT_OK(WaitForChanges(conn, 10, ASSERT_RESULT(TableFilter("t2"))));

  // Nothing but those 30 changes arrives, in particular not the writes to the CDC table itself.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 30);
}

TEST_F(CDCSDKCdcTableTest, LeaderChange) {
  // The test reads the tablet's leader once and expects the poller there. The load balancer would
  // move leaders while the cluster settles.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  const auto tablet_id = tablets[0].tablet_id();

  // The tservers that run a poller for the tablet.
  auto tservers_polling = [&]() {
    std::vector<size_t> result;
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      auto* consumer = test_cluster()->mini_tablet_server(i)->server()->GetXClusterConsumer();
      if (!consumer) {
        continue;
      }
      for (const auto& poller : consumer->TEST_ListPollers()) {
        if (poller->GetProducerTabletInfo().tablet_id == tablet_id) {
          result.push_back(i);
        }
      }
    }
    return result;
  };

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 10) i"));
  ASSERT_OK(WaitForChanges(conn, 10));

  size_t leader_index = 0;
  size_t follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
  ASSERT_EQ(tservers_polling(), std::vector<size_t>{leader_index});

  StartYbAdminClient();
  ASSERT_OK(StepDownLeader(follower_index, tablet_id));
  ASSERT_OK(LoggedWaitFor(
      [&] { return tservers_polling() == std::vector<size_t>{follower_index}; }, kTimeout,
      "Wait for the poller to move to the new leader"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'b' FROM generate_series(11, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));

  // The new poller may read changes again that the old one had already written. They land on the
  // same rows.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);
  std::set<int32_t> ids;
  for (const auto& change : ASSERT_RESULT(FetchChanges(conn))) {
    ids.insert(IdOf(change));
  }
  ASSERT_EQ(ids.size(), 20U);
}

TEST_F(CDCSDKCdcTableTest, SysCatalogChanges) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // Creating a table changes pg_class, which is one of the catalog tables of the stream.
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (id INT PRIMARY KEY)"));

  const auto database_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));
  const auto relname_attnum = ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT attnum::int FROM pg_attribute "
      "WHERE attrelid = 'pg_class'::regclass AND attname = 'relname'"));
  // The columns of a catalog table also come with their attribute number and type.
  const auto name_oid =
      ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>("SELECT 'name'::regtype::oid"));
  const auto pg_class_filter = Format(
      "replace(table_id::text, '-', '') = '$0'", GetPgsqlTableId(database_oid, kPgClassTableOid));
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        for (const auto& change : VERIFY_RESULT(FetchChanges(conn, pg_class_filter))) {
          if (!change.after) {
            continue;
          }
          const auto* column = FindColumn(*change.after, relname_attnum);
          if (column && column->type_oid() == name_oid && column->value().string_value() == "t3") {
            return true;
          }
        }
        return false;
      },
      kTimeout, "Wait for the row of t3 in pg_class"));
}

TEST_F(CDCSDKCdcTableTest, VirtualWalIsBlocked) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  auto stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // What the walsender does when replication is started on the slot.
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  InitVirtualWALForCDCRequestPB req;
  req.set_stream_id(stream_id.ToString());
  req.set_session_id(kVWALSessionId1);
  req.add_table_id(table.table_id());
  InitVirtualWALForCDCResponsePB resp;
  rpc::RpcController rpc;
  ASSERT_OK(cdc_proxy_->InitVirtualWALForCDC(req, &resp, &rpc));
  ASSERT_TRUE(resp.has_error());
  ASSERT_STR_CONTAINS(
      StatusFromPB(resp.error().status()).ToString(), "TEST_cdcsdk_enable_cdc_table");
}

// The walsender streams what the pollers wrote to the CDC table as the change stream of the slot,
// with the values converted the way they are from the CDC service.
TEST_F(CDCSDKCdcTableTest, WalsenderStreamsChanges) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  // The pollers only poll the tables of publications.
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_OK(CreateTextSlot(conn, "slot1"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'b')"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'c')"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (4, NULL)"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'a2' WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 3"));

  // The reader does not stream a change that reaches the table after a later one was read, so the
  // walsender starts once all of them are there.
  const auto t1_filter = ASSERT_RESULT(TableFilter("t1"));
  ASSERT_OK(WaitForChanges(conn, 6, t1_filter));

  const auto out_file = GetTestPath("walsender.out");
  auto recvlogical = ASSERT_RESULT(StartRecvlogical("slot1", out_file));

  const std::string kExpected =
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:1 v[text]:'a'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:2 v[text]:'b'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:3 v[text]:'c'\n"
      "table public.t1: INSERT: id[integer]:4 v[text]:null\n"
      "table public.t1: UPDATE: old-key: id[integer]:1 v[text]:'a' "
      "new-tuple: id[integer]:1 v[text]:'a2'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.t1: DELETE: id[integer]:3 v[text]:'c'\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out_file, kExpected)), kExpected);

  // A change made while the walsender runs follows the ones it has already streamed.
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (5, 'live')"));
  const std::string kLive =
      kExpected +
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:5 v[text]:'live'\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out_file, kLive)), kLive);

  ASSERT_OK(recvlogical->Kill(SIGINT));
  ASSERT_OK(recvlogical->Wait());
}

// A walsender restarted on the slot resumes after what the client confirmed, even when the client
// asks to start from 0/0, rather than send the slot's changes again from its creation.
TEST_F(CDCSDKCdcTableTest, WalsenderResumesFromSlot) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_OK(CreateTextSlot(conn, "slot1"));
  const auto t1_filter = ASSERT_RESULT(TableFilter("t1"));
  const auto confirmed_flush = [&conn]() {
    return conn.FetchRow<std::string>(
        "SELECT confirmed_flush_lsn::text FROM pg_replication_slots WHERE slot_name = 'slot1'");
  };
  const auto initial_flush = ASSERT_RESULT(confirmed_flush());

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'b')"));
  ASSERT_OK(WaitForChanges(conn, 2, t1_filter));

  // Fsync and report the position every second, so that the slot confirms it soon.
  const std::vector<std::string> kAckOften{"--fsync-interval=1", "--status-interval=1"};
  const auto out1 = GetTestPath("walsender1.out");
  auto recvlogical = ASSERT_RESULT(StartRecvlogical("slot1", out1, kAckOften));
  const std::string kFirst =
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:1 v[text]:'a'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:2 v[text]:'b'\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out1, kFirst)), kFirst);
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> { return VERIFY_RESULT(confirmed_flush()) != initial_flush; },
      kTimeout, "Wait for the slot to persist the confirmed position"));
  ASSERT_OK(recvlogical->Kill(SIGINT));
  ASSERT_OK(recvlogical->Wait());

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 'c')"));
  ASSERT_OK(WaitForChanges(conn, 3, t1_filter));

  // pg_recvlogical starts from 0/0, so only the slot's position keeps the first two out.
  const auto out2 = GetTestPath("walsender2.out");
  recvlogical = ASSERT_RESULT(StartRecvlogical("slot1", out2, kAckOften));
  const std::string kSecond =
      "BEGIN\n"
      "table public.t1: INSERT: id[integer]:3 v[text]:'c'\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out2, kSecond)), kSecond);
  // Nothing else follows.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out2, kSecond)), kSecond);

  ASSERT_OK(recvlogical->Kill(SIGINT));
  ASSERT_OK(recvlogical->Wait());
}

// Values of the types that DocDB stores differently from PostgreSQL reach the walsender as the
// QLValuePBs of an image. They must come out of the walsender as they went in.
TEST_F(CDCSDKCdcTableTest, WalsenderStreamsColumnTypes) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE ty (id INT PRIMARY KEY, b BIGINT, f DOUBLE PRECISION, ok BOOL, bt BYTEA, "
      "n NUMERIC, d DATE, ts TIMESTAMP, u UUID, j JSONB)"));
  ASSERT_OK(conn.Execute("ALTER TABLE ty REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE ty"));
  ASSERT_OK(CreateTextSlot(conn, "slot1"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO ty VALUES (1, 9223372036854775807, 1.5, true, '\\x0001ff', 123.456, "
      "'2026-11-20', '2026-11-20 10:11:12.123456', '123e4567-e89b-12d3-a456-426614174000', "
      "'{\"a\": [1, 2]}')"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO ty VALUES (2, -9223372036854775808, -0.25, false, '', -0.5, "
      "'2000-01-01', '1999-12-31 23:59:59', '00000000-0000-0000-0000-000000000000', 'null')"));
  ASSERT_OK(conn.Execute("INSERT INTO ty (id) VALUES (3)"));

  ASSERT_OK(WaitForChanges(conn, 3, ASSERT_RESULT(TableFilter("ty"))));

  const auto out_file = GetTestPath("walsender.out");
  auto recvlogical = ASSERT_RESULT(StartRecvlogical("slot1", out_file));

  const std::string kExpected =
      "BEGIN\n"
      "table public.ty: INSERT: id[integer]:1 b[bigint]:9223372036854775807 "
      "f[double precision]:1.5 ok[boolean]:true bt[bytea]:'\\x0001ff' n[numeric]:123.456 "
      "d[date]:'2026-11-20' ts[timestamp without time zone]:'2026-11-20 10:11:12.123456' "
      "u[uuid]:'123e4567-e89b-12d3-a456-426614174000' j[jsonb]:'{\"a\": [1, 2]}'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.ty: INSERT: id[integer]:2 b[bigint]:-9223372036854775808 "
      "f[double precision]:-0.25 ok[boolean]:false bt[bytea]:'\\x' n[numeric]:-0.5 "
      "d[date]:'2000-01-01' ts[timestamp without time zone]:'1999-12-31 23:59:59' "
      "u[uuid]:'00000000-0000-0000-0000-000000000000' j[jsonb]:'null'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.ty: INSERT: id[integer]:3 b[bigint]:null f[double precision]:null "
      "ok[boolean]:null bt[bytea]:null n[numeric]:null d[date]:null "
      "ts[timestamp without time zone]:null u[uuid]:null j[jsonb]:null\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out_file, kExpected)), kExpected);

  ASSERT_OK(recvlogical->Kill(SIGINT));
  ASSERT_OK(recvlogical->Wait());
}

// DocDB records no type OID for an enum, so the decoder takes its type from the relation.
TEST_F(CDCSDKCdcTableTest, WalsenderStreamsEnumAndArray) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute("CREATE TYPE mood AS ENUM ('sad', 'happy')"));
  ASSERT_OK(conn.Execute("CREATE TABLE en (id INT PRIMARY KEY, m mood, a INT[], t TEXT[])"));
  ASSERT_OK(conn.Execute("ALTER TABLE en REPLICA IDENTITY FULL"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE en"));
  ASSERT_OK(CreateTextSlot(conn, "slot1"));

  ASSERT_OK(conn.Execute("INSERT INTO en VALUES (1, 'happy', '{1,2,3}', '{a,\"b c\"}')"));
  ASSERT_OK(conn.Execute("INSERT INTO en VALUES (2, NULL, NULL, NULL)"));
  ASSERT_OK(WaitForChanges(conn, 2, ASSERT_RESULT(TableFilter("en"))));

  const auto out_file = GetTestPath("walsender.out");
  auto recvlogical = ASSERT_RESULT(StartRecvlogical("slot1", out_file));

  const std::string kExpected =
      "BEGIN\n"
      "table public.en: INSERT: id[integer]:1 m[public.mood]:'happy' a[integer[]]:'{1,2,3}' "
      "t[text[]]:'{a,\"b c\"}'\n"
      "COMMIT\n"
      "BEGIN\n"
      "table public.en: INSERT: id[integer]:2 m[public.mood]:null a[integer[]]:null "
      "t[text[]]:null\n"
      "COMMIT\n";
  ASSERT_EQ(ASSERT_RESULT(WaitForOutput(out_file, kExpected)), kExpected);

  ASSERT_OK(recvlogical->Kill(SIGINT));
  ASSERT_OK(recvlogical->Wait());
}

}  // namespace yb::cdc
