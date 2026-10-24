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

// Vector index searches restricted by the primary key: routing to the tablets that can hold the
// rows, and the per-tenant graph clusters of an index with vector_index_tenant_key_components.

#include <numeric>

#include "yb/client/table.h"

#include "yb/docdb/doc_vector_index.h"

#include "yb/dockv/partition.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/metrics.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_vector_index_exact);
DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_bool(vector_index_enable_pk_routing);
DECLARE_bool(vector_index_store_payload);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_string(vector_index_backend);
DECLARE_uint32(vector_index_tenant_key_components);

METRIC_DECLARE_event_stats(vector_index_partitions_queried);

namespace yb::docdb {

extern bool TEST_vector_index_filter_allowed;

} // namespace yb::docdb

namespace yb::tablet {

extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(Backfill);

// Routing does not depend on the vector index engine or the row packing, so these tests run with
// the defaults only.
class PgVectorIndexPkRoutingTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "yb_hnsw_hnswlib";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = true;
    // Auto analyze scans the tables, which TEST_fail_on_seq_scan_with_vector_indexes rejects.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze_infra) = false;
    // The tests expect the tablets they create.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = false;
    PgMiniTestBase::SetUp();
    tablet::TEST_fail_on_seq_scan_with_vector_indexes = true;
  }

  static constexpr int kNumTenants = 20;
  static constexpr int kRowsPerTenant = 10;
  static constexpr int kNumTablets = 4;

  struct QueryStats {
    std::vector<int32_t> ids;
    // Partitions searched over all fetch rounds of the query, and the number of rounds.
    int64_t partitions = 0;
    uint64_t rounds = 0;
  };

  std::pair<int64_t, uint64_t> PartitionsQueried() {
    int64_t sum = 0;
    uint64_t count = 0;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      const auto* stats = cluster_->mini_tablet_server(i)->metric_entity()
          .FindOrCreateMetric<EventStats>(&METRIC_vector_index_partitions_queried)->underlying();
      sum += stats->TotalSum();
      count += stats->TotalCount();
    }
    return {sum, count};
  }

  Result<QueryStats> Query(PGConn& conn, const std::string& query) {
    auto [sum_before, count_before] = PartitionsQueried();
    QueryStats result;
    result.ids = VERIFY_RESULT(conn.FetchRows<int32_t>(query));
    auto [sum_after, count_after] = PartitionsQueried();
    result.partitions = sum_after - sum_before;
    result.rounds = count_after - count_before;
    return result;
  }

  // Embedding of row (tenant, id) is [tenant, id, 0], so the rows of a tenant ordered by distance
  // to [tenant, 0, 0] are ordered by id.
  // With backfill the rows are inserted before the index is created.
  Status CreateAndFill(PGConn& conn, const std::string& table, const std::string& key_columns,
                       const std::string& primary_key, const std::string& split,
                       Backfill backfill = Backfill::kFalse) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 ($1, embedding vector(3), PRIMARY KEY ($2)) $3",
        table, key_columns, primary_key, split));
    const auto create_index = Format(
        "CREATE INDEX ON $0 USING ybhnsw (embedding vector_l2_ops)", table);
    if (!backfill) {
      RETURN_NOT_OK(conn.Execute(create_index));
    }
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT t, i, vector('[' || t || ', ' || i || ', 0]') "
        "FROM generate_series(0, $1) t, generate_series(0, $2) i",
        table, kNumTenants - 1, kRowsPerTenant - 1));
    return backfill ? conn.Execute(create_index) : Status::OK();
  }

  static std::string KnnQuery(
      const std::string& table, const std::string& where, int tenant, int limit) {
    return Format(
        "/*+IndexScan($0 $0_embedding_idx)*/ SELECT id FROM $0 WHERE $1 "
        "ORDER BY embedding <-> '[$2, 0, 0]' LIMIT $3",
        table, where, tenant, limit);
  }

  static std::vector<int32_t> Ids(int32_t first, int32_t count) {
    std::vector<int32_t> result(count);
    std::iota(result.begin(), result.end(), first);
    return result;
  }

  void TestTenants(
      const std::string& primary_key, const std::string& split,
      Backfill backfill = Backfill::kFalse);

  // Index of the partition of table_name that holds hash code hash.
  Result<size_t> HashPartition(const std::string& table_name, uint16_t hash) {
    auto table = VERIFY_RESULT(client_->OpenTable(
        VERIFY_RESULT(GetTableIDFromTableName(table_name))));
    return client::FindPartitionStartIndex(
        table->GetVersionedPartitions()->keys,
        dockv::PartitionSchema::EncodeMultiColumnHashValue(hash));
  }
};

TEST_F(PgVectorIndexPkRoutingTest, HashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "th", "tenant INT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("th", Format("tenant = $0", tenant), tenant, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // Without a filter on the key, and with routing disabled, every partition is searched.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "id < 3", 4, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = false;
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4", 4, 5)));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = true;
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // A non-integer cross-type equality isn't routed, but still returns the right rows.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4.0", 4, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // With several equalities on the key, the one with the fewest values bounds the search.
  for (const auto* where : {"tenant = 3 AND tenant IN (3, 4)", "tenant IN (3, 4) AND tenant = 3"}) {
    SCOPED_TRACE(where);
    stats = ASSERT_RESULT(Query(conn, KnnQuery("th", where, 3, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // IN list: one partition per distinct partition of the tenants.
  auto hashes = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT yb_hash_code(t) FROM generate_series(0, $0) t ORDER BY t", kNumTenants - 1)));
  std::vector<size_t> partition_of_tenant;
  for (auto hash : hashes) {
    partition_of_tenant.push_back(ASSERT_RESULT(HashPartition("th", hash)));
  }
  int other_tenant = 1;
  while (partition_of_tenant[other_tenant] == partition_of_tenant[0]) {
    ++other_tenant;
    ASSERT_LT(other_tenant, kNumTenants);
  }
  // Fetch every row of both tenants, so a missing partition would lose rows.
  stats = ASSERT_RESULT(Query(
      conn, KnnQuery("th", Format("tenant IN (0, $0)", other_tenant), 0, 2 * kRowsPerTenant)));
  std::ranges::sort(stats.ids);
  std::vector<int32_t> expected_ids;
  for (int id = 0; id != kRowsPerTenant; ++id) {
    expected_ids.insert(expected_ids.end(), {id, id});
  }
  ASSERT_EQ(stats.ids, expected_ids);
  ASSERT_EQ(stats.partitions, 2);

  // Prepared statement with a generic plan: the parameter is bound at execution.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(int, vector) AS /*+IndexScan(th)*/ SELECT id FROM th WHERE tenant = $1 "
      "ORDER BY embedding <-> $2 LIMIT 4"));
  for (int tenant : {3, 7}) {
    auto stats = ASSERT_RESULT(Query(
        conn, Format("EXECUTE knn($0, '[$0, 0, 0]')", tenant)));
    ASSERT_EQ(stats.ids, Ids(0, 4));
    ASSERT_EQ(stats.partitions, 1);
  }
  ASSERT_OK(conn.Execute("RESET plan_cache_mode"));

  // Nested loop: the inner scan is routed again on each rescan.
  stats = ASSERT_RESULT(Query(conn,
      "/*+NestLoop(t th) Leading((t th)) IndexScan(th)*/ "
      "SELECT x.id FROM (VALUES (2), (9)) t(tenant), LATERAL ("
      "  SELECT id FROM th WHERE th.tenant = t.tenant "
      "  ORDER BY embedding <-> '[0, 0, 0]' LIMIT 2) x"));
  ASSERT_EQ(stats.ids, (std::vector<int32_t>{0, 1, 0, 1}));
  ASSERT_EQ(stats.partitions, 2);

  // The join rejects the first rows returned by the vector index, so Postgres fetches more rounds.
  // The pruned partitions must not keep the query waiting for them.
  ASSERT_OK(conn.Execute("CREATE TABLE keep (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO keep VALUES (6), (7), (8)"));
  stats = ASSERT_RESULT(Query(conn,
      "SELECT th.id FROM th INNER JOIN keep AS k ON th.id = k.id WHERE th.tenant = 5 "
      "ORDER BY th.embedding <-> '[5, 0, 0]' LIMIT 3"));
  ASSERT_EQ(stats.ids, Ids(6, 3));
  ASSERT_GE(stats.rounds, 2);
  ASSERT_LE(stats.partitions, static_cast<int64_t>(stats.rounds));
}

TEST_F(PgVectorIndexPkRoutingTest, MultiColumnHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tm", "tenant INT, id INT", "(tenant, id) HASH",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3 AND id = 2", 3, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{2});
  ASSERT_EQ(stats.partitions, 1);

  // Only some of the hash columns: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3", 3, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, TextHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tt", "tenant TEXT COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  // Expressions with a non-C collation are not pushed down, so their key equalities can't be
  // routed. Their results must still be correct.
  ASSERT_OK(CreateAndFill(
      conn, "tt_icu", "tenant TEXT COLLATE \"en-US-x-icu\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt", Format("tenant = '$0'", tenant), tenant, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);

    stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt_icu", Format("tenant = '$0'", tenant), tenant, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // varchar has no equality operator of its own: the column is compared with text's operator, and
  // the value can be text or varchar.
  ASSERT_OK(CreateAndFill(
      conn, "tv", "tenant VARCHAR(20) COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  for (const auto* value : {"'7'", "'7'::varchar", "'7'::text", "'7'::varchar(5)"}) {
    SCOPED_TRACE(value);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", Format("tenant = $0", value), 7, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);
  }
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", "tenant IN ('7', '7')", 7, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, 1);
}

// Integer literals are int4 and parameters can be int8, so equalities on integer keys are often
// cross-type (e.g. int8 = int4). Those are routed after converting the value to the column type.
TEST_F(PgVectorIndexPkRoutingTest, IntegerCrossTypeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tb", "tenant BIGINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  ASSERT_OK(CreateAndFill(
      conn, "ts", "tenant SMALLINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (const auto* where : {"tenant = 6", "6 = tenant", "tenant = 6::smallint", "tenant IN (6)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tb", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }
  for (const auto* where : {"tenant = 6", "tenant = 6::bigint", "tenant IN (6::bigint)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // A value outside the smallint range matches no row, so the column isn't routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant = 70000", 6, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{});
  ASSERT_EQ(stats.partitions, kNumTablets);
  stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant IN (6, 70000)", 6, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // A bigint parameter against a smallint column, with a generic plan.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(bigint, vector) AS /*+IndexScan(ts ts_embedding_idx)*/ SELECT id FROM ts "
      "WHERE tenant = $1 ORDER BY embedding <-> $2 LIMIT 4"));
  stats = ASSERT_RESULT(Query(conn, "EXECUTE knn(9, '[9, 0, 0]')"));
  ASSERT_EQ(stats.ids, Ids(0, 4));
  ASSERT_EQ(stats.partitions, 1);
}

TEST_F(PgVectorIndexPkRoutingTest, FloatKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tf", "tenant FLOAT8, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  // -0 equals 0 but has a different key encoding, so float keys are never routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tf", "tenant = '-0'", 0, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, RangeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  // Partitions: [, 5), [5, (10, 5)), [(10, 5), 15), [15, ).
  ASSERT_OK(CreateAndFill(
      conn, "tr", "tenant INT, id INT", "tenant ASC, id ASC",
      "SPLIT AT VALUES ((5), (10, 5), (15))"));

  for (auto [tenant, partitions] : std::initializer_list<std::pair<int, int64_t>>{
           // The split point (5) sorts after the prefix of tenant 5, so both of its neighbours
           // are searched.
           {2, 1}, {5, 2}, {7, 1}, {10, 2}, {12, 1}, {15, 2}, {19, 1}}) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tr", Format("tenant = $0", tenant), tenant, 7)));
    ASSERT_EQ(stats.ids, Ids(0, 7));
    ASSERT_EQ(stats.partitions, partitions);
  }

  // A prefix of two range columns.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "tenant = 10 AND id = 7", 10, 3)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{7});
  ASSERT_EQ(stats.partitions, 1);

  // No equality on the leading range column: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "id = 7", 10, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

// With vector_index_tenant_key_components the vectors of each tenant are only linked to each other,
// and a search visits only the tenant its key equalities select. The query vector is closest to
// the vectors of tenant 0, so other tenants are found only through their own entry points.
void PgVectorIndexPkRoutingTest::TestTenants(
    const std::string& primary_key, const std::string& split, Backfill backfill) {
  // Exact search scans all vectors, ignoring tenants.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_key_components) = 1;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  constexpr auto kTable = "tt";
  ASSERT_OK(CreateAndFill(conn, kTable, "tenant INT, id INT", primary_key, split, backfill));

  auto check = [&conn](int rows_per_tenant) {
    // Without deletions, rows of the searched tenant need no check.
    docdb::TEST_vector_index_filter_allowed = false;
    for (int tenant = 0; tenant != kNumTenants; ++tenant) {
      SCOPED_TRACE(Format("tenant: $0", tenant));
      auto ids = ASSERT_RESULT(conn.FetchRows<int32_t>(
          KnnQuery(kTable, Format("tenant = $0", tenant), 0, rows_per_tenant)));
      ASSERT_EQ(ids, Ids(0, rows_per_tenant));
    }
    docdb::TEST_vector_index_filter_allowed = true;

    // Equalities on more key columns than the tenant has.
    auto ids = ASSERT_RESULT(conn.FetchRows<int32_t>(
        KnnQuery(kTable, "tenant = 10 AND id IN (7, 8)", 0, 3)));
    ASSERT_EQ(ids, (std::vector<int32_t>{7, 8}));

    for (const auto* where : {"id < 3", "tenant IN (3, 12)"}) {
      SCOPED_TRACE(where);
      auto result = conn.FetchRows<int32_t>(KnnQuery(kTable, where, 0, 3));
      ASSERT_NOK_STR_CONTAINS(result, "equality conditions with a single value");
    }
  };

  ASSERT_NO_FATALS(check(kRowsPerTenant));

  // The rows of an open transaction come from its intents, not from the index, and the tenant
  // equality is not pushed down, so the rows of other tenants must still be left out.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (1, 1000, '[0, 0, 0]'), (2, 1001, '[0, 0, 0]')", kTable));
  auto ids = ASSERT_RESULT(conn.FetchRows<int32_t>(KnnQuery(kTable, "tenant = 1", 0, 3)));
  ASSERT_EQ(ids, (std::vector<int32_t>{1000, 0, 1}));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_NO_FATALS(check(kRowsPerTenant));

  // A second chunk, then both chunks merged.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT t, i, vector('[' || t || ', ' || i || ', 0]') "
      "FROM generate_series(0, $1) t, generate_series($2, $3) i",
      kTable, kNumTenants - 1, kRowsPerTenant, 2 * kRowsPerTenant - 1));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_NO_FATALS(check(2 * kRowsPerTenant));
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    ASSERT_OK(index->Compact());
    ASSERT_OK(index->WaitForCompaction());
  }
  ASSERT_NO_FATALS(check(2 * kRowsPerTenant));

  // The deleted vector and the vector replaced by the update are left in the index, so rows are
  // checked again.
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE tenant = 4 AND id = 0", kTable));
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET embedding = '[4, 100, 0]' WHERE tenant = 4 AND id = 1", kTable));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ids = ASSERT_RESULT(conn.FetchRows<int32_t>(KnnQuery(kTable, "tenant = 4", 0, 3)));
  ASSERT_EQ(ids, Ids(2, 3));
}

TEST_F(PgVectorIndexPkRoutingTest, TenantsHashKey) {
  TestTenants("(tenant) HASH, id", Format("SPLIT INTO $0 TABLETS", kNumTablets));
}

TEST_F(PgVectorIndexPkRoutingTest, TenantsRangeKey) {
  TestTenants("tenant ASC, id ASC", "SPLIT AT VALUES ((5), (10, 5), (15))");
}

// Backfill inserts the vectors of each tenant back to back.
TEST_F(PgVectorIndexPkRoutingTest, TenantsBackfill) {
  TestTenants(
      "(tenant) HASH, id", Format("SPLIT INTO $0 TABLETS", kNumTablets), Backfill::kTrue);
}

} // namespace yb::pgwrapper
