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

#include "yb/yql/pgwrapper/pg_wrapper.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/result.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"

using std::string;
using std::unique_ptr;

using yb::client::QLDmlTestBase;
using yb::client::YBSchemaBuilder;
using yb::client::TableHandle;
using yb::client::kTableName;

namespace yb {
namespace pgwrapper {

class PgWrapperTest : public QLDmlTestBase {
 protected:
  virtual void SetUp() override {
    QLDmlTestBase::SetUp();
    CreateSchema();

    pg_port_ = GetFreePort(&pg_port_file_lock_);
    string test_dir;
    CHECK_OK(Env::Default()->GetTestDirectory(&test_dir));
    pg_data_dir_ = JoinPathSegments(test_dir, "pg_data");
  }

  void CreateSchema() {
    YBSchemaBuilder b;
    b.AddColumn("h1")->Type(INT32)->HashPrimaryKey()->NotNull();
    b.AddColumn("h2")->Type(STRING)->HashPrimaryKey()->NotNull();
    b.AddColumn("r1")->Type(INT32)->PrimaryKey()->NotNull();
    b.AddColumn("r2")->Type(STRING)->PrimaryKey()->NotNull();
    b.AddColumn("c1")->Type(INT32);
    b.AddColumn("c2")->Type(STRING);

    ASSERT_OK(table_.Create(kTableName, CalcNumTablets(3), client_.get(), &b));
  }

  string pg_data_dir_;
  uint16_t pg_port_ = 0;
  TableHandle table_;

 private:
  unique_ptr<FileLock> pg_port_file_lock_;
};

TEST_F(PgWrapperTest, TestStartStop) {
  PgWrapperConf conf {
    pg_data_dir_,
    pg_port_
  };
  PgWrapper postgres(conf);

  ASSERT_OK(postgres.InitDB());
  ASSERT_OK(postgres.Start());
}

}  // namespace pgwrapper
}  // namespace yb
