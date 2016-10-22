//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_YBSQL_TEST_BASE_H_
#define YB_SQL_YBSQL_TEST_BASE_H_

#include <string>
#include <gtest/gtest.h>

#include "yb/sql/ybsql.h"
#include "yb/util/test_util.h"

namespace yb {
namespace sql {

using std::string;

// Base class for all SQL test cases.
class YbSqlTestBase : public YBTest {
 public:
  YbSqlTestBase() :
    ybsql_(std::unique_ptr<YbSql>(new YbSql())) {
  }

  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();
  }

  virtual void TearDown() OVERRIDE {
    YBTest::TearDown();
  }

 protected:
  YbSql::UniPtr ybsql_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_YBSQL_TEST_BASE_H_
