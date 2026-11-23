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
//

#include <string>
#include <unordered_map>
#include <vector>

#include "yb/ash/latency_ledger.h"

#include "yb/common/json_util.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/curl_util.h"
#include "yb/util/faststring.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pggate/ybc_latency_ledger.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_int32(ysql_yb_latency_ledger_buffer_size);
DECLARE_double(ysql_yb_latency_ledger_sample_rate);
DECLARE_int32(ysql_yb_latency_ledger_min_duration_ms);

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

struct LedgerEntry {
  uint32_t depth;
  uint32_t phase;
  std::string name;
  int64_t nanos;
};

struct LedgerRecord {
  uint64_t seq;
  int64_t duration_ns;
  uint64_t query_id;
  uint32_t flags;
  int64_t cpu_ns;
  std::vector<LedgerEntry> entries;

  int64_t Sum(uint32_t depth, std::optional<uint32_t> phase = {}) const {
    int64_t result = 0;
    for (const auto& entry : entries) {
      if (entry.depth == depth && (!phase || entry.phase == *phase)) {
        result += entry.nanos;
      }
    }
    return result;
  }

  bool Has(uint32_t min_depth, const std::string& name) const {
    for (const auto& entry : entries) {
      if (entry.depth >= min_depth && entry.name == name) {
        return true;
      }
    }
    return false;
  }

  std::string ToString() const {
    std::string result = Format(
        "seq $0 duration $1 query $2 flags $3:", seq, duration_ns, query_id, flags);
    for (const auto& entry : entries) {
      result += Format(" [$0/$1/$2 $3]", entry.depth, entry.phase, entry.name, entry.nanos);
    }
    return result;
  }
};

struct LedgerDump {
  std::vector<LedgerRecord> records;
  uint64_t next_seq = 0;
  uint64_t resume_after = 0;
  uint64_t lost = 0;
};

constexpr uint32_t kSampledSession = YB_LEDGER_FLAG_SAMPLED_SESSION;

} // namespace

class PgLatencyLedgerTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_latency_ledger_buffer_size) = 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_latency_ledger_sample_rate) = 1.0;
    PgMiniTestBase::SetUp();
  }

  Result<LedgerDump> FetchLedger(uint64_t since = 0) {
    const auto url = Format(
        "http://$0:$1/latency-ledger?since=$2", pg_host_port().host(),
        FLAGS_pgsql_proxy_webserver_port, since);
    EasyCurl curl;
    faststring buf;
    RETURN_NOT_OK(WaitFor(
        [&curl, &url, &buf]() -> Result<bool> { return curl.FetchURL(url, &buf).ok(); },
        10s * kTimeMultiplier, "Requesting /latency-ledger", 100ms));
    auto doc = VERIFY_RESULT(common::ParseJson(std::string_view(buf.char_data(), buf.size())));
    SCHECK(!doc.HasMember("error"), IllegalState, buf.ToString());

    std::unordered_map<std::string, std::string> ysql_names;
    std::unordered_map<std::string, std::string> tserver_names;
    const auto& names = doc["names"];
    for (const auto& [kind, out] : {std::pair{"ysql", &ysql_names},
                                    std::pair{"tserver", &tserver_names}}) {
      for (const auto& member : names[kind].GetObject()) {
        (*out)[member.name.GetString()] = member.value.GetString();
      }
    }

    LedgerDump dump;
    dump.next_seq = doc["next_seq"].GetUint64();
    dump.resume_after = doc["resume_after"].GetUint64();
    dump.lost = doc["lost"].GetUint64();
    for (const auto& raw : doc["records"].GetArray()) {
      LedgerRecord record{
        .seq = raw[0].GetUint64(),
        .duration_ns = raw[2].GetInt64(),
        .query_id = raw[4].GetUint64(),
        .flags = static_cast<uint32_t>(raw[7].GetUint64()),
        .cpu_ns = raw[8].GetInt64(),
        .entries = {},
      };
      const auto& entries = raw[10].GetArray();
      for (rapidjson::SizeType i = 0; i + 1 < entries.Size(); i += 2) {
        const auto key = static_cast<uint32_t>(entries[i].GetUint64());
        const auto depth = ash::LedgerKeyDepth(key);
        const auto code = std::to_string(ash::LedgerKeyCode(key));
        auto name = depth == 0 ? ysql_names[code] : tserver_names[code];
        record.entries.push_back(LedgerEntry{
          .depth = depth,
          .phase = ash::LedgerKeyPhase(key),
          .name = std::move(name),
          .nanos = static_cast<int64_t>(entries[i + 1].GetUint64()) << YB_LEDGER_DURATION_SHIFT,
        });
      }
      dump.records.push_back(std::move(record));
    }
    return dump;
  }

  Result<uint64_t> QueryId(PGConn& conn, const std::string& prefix) {
    return static_cast<uint64_t>(VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
        "SELECT queryid FROM pg_stat_statements WHERE query LIKE '$0%'", prefix))));
  }

  // Finished round trips are written to the ring right before ReadyForQuery, so wait for the
  // last one of the workload to show up.
  Result<LedgerDump> WaitForQuery(uint64_t query_id) {
    LedgerDump dump;
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      dump = VERIFY_RESULT(FetchLedger());
      for (const auto& record : dump.records) {
        if (record.query_id == query_id) {
          return true;
        }
      }
      return false;
    }, 10s * kTimeMultiplier, "Waiting for the ledger of the query"));
    return dump;
  }
};

// Depth 0 accounts every nanosecond of a round trip, and each deeper hop fits into the waits of
// the hop above it.
TEST_F(PgLatencyLedgerTest, RoundTripsAddUp) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 100) i"));
  ASSERT_OK(conn.Fetch("SELECT * FROM t WHERE k = 1"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE t SET v = v + 1 WHERE k = 2"));
  ASSERT_OK(conn.CommitTransaction());
  const auto insert_id = ASSERT_RESULT(QueryId(conn, "INSERT INTO t"));
  const auto commit_id = ASSERT_RESULT(QueryId(conn, "COMMIT"));

  const auto dump = ASSERT_RESULT(WaitForQuery(commit_id));
  ASSERT_GT(dump.records.size(), 5);
  bool found_insert = false;
  bool found_commit = false;
  for (const auto& record : dump.records) {
    SCOPED_TRACE(record.ToString());
    ASSERT_TRUE(record.flags & kSampledSession);
    // Each entry loses less than one duration unit to rounding.
    const auto depth0 = record.Sum(0);
    ASSERT_LE(depth0, record.duration_ns);
    ASSERT_GE(depth0 + static_cast<int64_t>(record.entries.size() << YB_LEDGER_DURATION_SHIFT),
              record.duration_ns);
    for (uint32_t depth = 1; depth <= 3; ++depth) {
      // A hop can last a little longer than the wait above it: its ledger ends before it hands
      // the response back, and the next hop up counts the handoff.
      ASSERT_LE(record.Sum(depth), record.Sum(depth - 1) + 100'000);
    }
    if (record.query_id == insert_id) {
      found_insert = true;
      ASSERT_TRUE(record.Has(1, "ShmemRequest"));
      ASSERT_TRUE(record.Has(1, "ShmemResponse"));
      ASSERT_TRUE(record.Has(2, "Transport"));
      ASSERT_TRUE(record.Has(2, "OnCpu_Active"));
      ASSERT_GT(record.Sum(2, YB_LEDGER_PHASE_EXECUTE), 0);
    }
    if (record.query_id == commit_id) {
      found_commit = true;
      ASSERT_GT(record.Sum(0, YB_LEDGER_PHASE_COMMIT), 0);
      ASSERT_TRUE(record.flags & YB_LEDGER_FLAG_COMMITTED);
      // The commit's transaction status update.
      ASSERT_GT(record.Sum(2, YB_LEDGER_PHASE_COMMIT), 0);
    }
  }
  ASSERT_TRUE(found_insert);
  ASSERT_TRUE(found_commit);
}

// Unsampled backends record only the round trips that take at least the minimum duration.
TEST_F(PgLatencyLedgerTest, MinDuration) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET yb_latency_ledger_sample_rate = 0"));
  ASSERT_OK(conn.Execute("SET yb_latency_ledger_min_duration_ms = 150"));
  const auto since = ASSERT_RESULT(FetchLedger()).next_seq;
  ASSERT_OK(conn.Fetch("SELECT 1"));
  ASSERT_OK(conn.Fetch("SELECT pg_sleep(0.3)"));
  ASSERT_OK(conn.Fetch("SELECT 2"));
  const auto sleep_id = ASSERT_RESULT(QueryId(conn, "SELECT pg_sleep"));

  LedgerDump dump;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    dump = VERIFY_RESULT(FetchLedger(since));
    return !dump.records.empty();
  }, 10s * kTimeMultiplier, "Waiting for the slow round trip"));
  ASSERT_EQ(dump.records.size(), 1);
  const auto& record = dump.records.front();
  SCOPED_TRACE(record.ToString());
  ASSERT_EQ(record.query_id, sleep_id);
  ASSERT_FALSE(record.flags & kSampledSession);
  ASSERT_GE(record.duration_ns, 300'000'000);
  ASSERT_TRUE(record.Has(0, "Timeout:PgSleep"));
}

// Readers learn how many records the ring overwrote before they were read.
TEST_F(PgLatencyLedgerTest, RingWraps) {
  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i != 1500; ++i) {
    ASSERT_OK(conn.Fetch("SELECT 1"));
  }
  const auto dump = ASSERT_RESULT(FetchLedger());
  ASSERT_GE(dump.next_seq, 1500);
  ASSERT_EQ(dump.records.size(), FLAGS_ysql_yb_latency_ledger_buffer_size);
  ASSERT_EQ(dump.lost + dump.records.size(), dump.next_seq);
  ASSERT_EQ(dump.resume_after, dump.next_seq);
  ASSERT_EQ(dump.records.back().seq, dump.next_seq);
}

} // namespace yb::pgwrapper
