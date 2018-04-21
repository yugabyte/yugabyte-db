//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/test/pg_psql_test.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace pgsql {

class PgRunTest : public PgPsqlTest {
 public:
  PgRunTest() {
  }
};

TEST_F(PgRunTest, TestBasic) {
#if 0
// TODO(neil) Jenkins does not like this test settup although the test can be run locally.
// I would have to work with Mikhail to make sure the test can be run on Jenkins before enabling
// this test.
  CHECK_OK(RunTestSuite("basic"));
#endif
  CHECK(true);
}

} // namespace pgsql
} // namespace yb
