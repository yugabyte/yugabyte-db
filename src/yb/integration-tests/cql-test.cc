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

#include "yb/integration-tests/cql_test_base.h"

#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

DECLARE_int64(cql_processors_limit);

namespace yb {

class CqlTest : public CqlTestBase {
 public:
  virtual ~CqlTest() = default;
};

TEST_F(CqlTest, ProcessorsLimit) {
  constexpr int kSessions = 10;
  FLAGS_cql_processors_limit = 1;

  std::vector<CassandraSession> sessions;
  bool has_failures = false;
  for (int i = 0; i != kSessions; ++i) {
    auto session = EstablishSession(driver_.get());
    if (!session.ok()) {
      LOG(INFO) << "Establish session failure: " << session.status();
      ASSERT_TRUE(session.status().IsServiceUnavailable());
      has_failures = true;
    } else {
      sessions.push_back(std::move(*session));
    }
  }

  ASSERT_TRUE(has_failures);
}

TEST_F(CqlTest, TestUpdateListIndexAfterOverwrite) {
  auto session = ASSERT_RESULT(EstablishSession(driver_.get()));
  auto cql = [&](const std::string query) {
    ASSERT_OK(session.ExecuteQuery(query));
  };
  cql("CREATE TABLE test(h INT, v LIST<INT>, PRIMARY KEY(h))");
  cql("INSERT INTO test (h, v) VALUES (1, [1, 2, 3])");

  auto select = [&]() -> Result<string> {
    auto result = VERIFY_RESULT(session.ExecuteWithResult("SELECT * FROM test"));
    auto iter = result.CreateIterator();
    DFATAL_OR_RETURN_ERROR_IF(!iter.Next(), STATUS(NotFound, "Did not find result in test table."));
    auto row = iter.Row();
    auto key = row.Value(0).As<int>();
    EXPECT_EQ(key, 1);
    return row.Value(1).ToString();
  };

  cql("UPDATE test SET v = [4, 5, 6] where h = 1");
  cql("UPDATE test SET v[0] = 7 WHERE h = 1");
  auto res1 = ASSERT_RESULT(select());
  EXPECT_EQ(res1, "[7, 5, 6]");

  cql("INSERT INTO test (h, v) VALUES (1, [10, 11, 12])");
  cql("UPDATE test SET v[0] = 8 WHERE h = 1");
  auto res2 = ASSERT_RESULT(select());
  EXPECT_EQ(res2, "[8, 11, 12]");
}

} // namespace yb
