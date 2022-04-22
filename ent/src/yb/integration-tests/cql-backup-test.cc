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

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using std::make_unique;
using std::string;
using std::unique_ptr;

DECLARE_bool(TEST_mini_cluster_mode);

namespace yb {

const string kDefaultTableName = "users";

class CqlBackupTest : public CqlTestBase<MiniCluster> {
 public:
  virtual ~CqlBackupTest() = default;

  void SetUp() override {
    FLAGS_TEST_mini_cluster_mode = true; // Provide correct '--fs_data_dirs' via TS Web UI.
    CqlTestBase<MiniCluster>::SetUp();

    backup_dir_ = GetTempDir("backup");
    session_ = make_unique<CassandraSession>(
        ASSERT_RESULT(EstablishSession(driver_.get())));
  }

  void cql(const string& query) {
    ASSERT_OK(session_->ExecuteQuery(query));
  }

  void createTestTable(const string& table_name = kDefaultTableName) {
    cql("CREATE TABLE " + table_name + "(userid INT PRIMARY KEY, fullname TEXT)");
    cql("INSERT INTO " + table_name + " (userid, fullname) values (1, 'yb');");

    auto result = ASSERT_RESULT(session_->ExecuteWithResult("SELECT count(*) FROM " + table_name));
    auto iterator = result.CreateIterator();
    ASSERT_TRUE(iterator.Next());
    auto count = iterator.Row().Value(0).As<int64>();
    EXPECT_EQ(count, 1);
  }

 protected:
  string backup_dir_;
  unique_ptr<CassandraSession> session_;
};

}  // namespace yb
