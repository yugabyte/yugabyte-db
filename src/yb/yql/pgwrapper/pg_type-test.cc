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

#include "ybgate/ybgate_api-test.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb {
namespace pgwrapper {

class PgTypeTest : public PgMiniTestBase {
};

TEST_F(PgTypeTest, YbTypeDetailsMatch) {
  auto conn = ASSERT_RESULT(Connect());
  auto rows = ASSERT_RESULT((conn.FetchRows<PGOid, std::string, int16_t, bool, char>(
    "SELECT oid, typname, typlen, typbyval, typalign FROM pg_type")));
  int nfound = 0;
  for (const auto& [pgtypoid, pgtypname, pgtyplen, pgtypbyval, pgtypalign] : rows) {
    int16_t ybtyplen;
    bool ybtypbyval;
    char ybtypalign;
    if (YbTypeDetailsTest(pgtypoid, &ybtyplen, &ybtypbyval, &ybtypalign)) {
      ++nfound;
      ASSERT_EQ(ybtyplen, pgtyplen)
        << "Mismatched typlen for type " << pgtypname << "(" << pgtypoid << "), "
        << "expected: " << pgtyplen << ", found: " << ybtyplen;
      ASSERT_EQ(ybtypbyval, pgtypbyval)
        << "Mismatched typbyval for type " << pgtypname << "(" << pgtypoid << "), "
        << "expected: " << pgtypbyval << ", found: " << ybtypbyval;
      ASSERT_EQ(ybtypalign, pgtypalign)
        << "Mismatched typalign for type " << pgtypname << "(" << pgtypoid << "), "
        << "expected: " << pgtypalign << ", found: " << ybtypalign;
    }
  }
  LOG(INFO) << "Mismatched 0 out of " << nfound << " tested";
  ASSERT_GT(nfound, 0);
}

} // namespace pgwrapper
} // namespace yb
