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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/qlexpr/index.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

namespace yb::pgwrapper {

class PgVectorIndexTest : public PgMiniTestBase {
 protected:
  void TestSimple(bool colocated);
};

void PgVectorIndexTest::TestSimple(bool colocated) {
  auto conn = ASSERT_RESULT(Connect());
  std::string create_suffix;
  if (colocated) {
    create_suffix = " WITH (COLOCATED = 1)";
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(ConnectToDB("colocated_db"));
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector VERSION '0.4.4-yb-1.2'"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))" + create_suffix));

  ASSERT_OK(conn.Execute("CREATE INDEX ON test USING ybdummyann (embedding vector_l2_ops)"));

  size_t num_found_peers = 0;
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet_safe());
    if (tablet->table_type() != TableType::PGSQL_TABLE_TYPE) {
      continue;
    }
    auto& metadata = *tablet->metadata();
    auto tables = metadata.GetAllColocatedTables();
    tablet::TableInfoPtr main_table_info;
    tablet::TableInfoPtr index_table_info;
    for (const auto& table_id : tables) {
      auto table_info = ASSERT_RESULT(metadata.GetTableInfo(table_id));
      LOG(INFO) << "Table: " << table_info->ToString();
      if (table_info->table_name == "test") {
        main_table_info = table_info;
      } else if (table_info->index_info) {
        index_table_info = table_info;
      }
    }
    if (!main_table_info) {
      continue;
    }
    ++num_found_peers;
    ASSERT_ONLY_NOTNULL(index_table_info);
    ASSERT_EQ(index_table_info->index_info->indexed_table_id(), main_table_info->table_id);
  }

  ASSERT_NE(num_found_peers, 0);

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test ORDER BY embedding <-> '[1.0, 0.4, 0.3]' LIMIT 5"));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");
}

TEST_F(PgVectorIndexTest, Simple) {
  TestSimple(/* colocated= */ false);
}

TEST_F(PgVectorIndexTest, SimpleColocated) {
  TestSimple(/* colocated= */ true);
}

}  // namespace yb::pgwrapper
