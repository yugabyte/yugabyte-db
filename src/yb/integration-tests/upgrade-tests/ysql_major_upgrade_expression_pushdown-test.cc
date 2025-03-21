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

#include <regex>
#include "yb/integration-tests/upgrade-tests/pg15_upgrade_test_base.h"

#include "yb/gutil/strings/split.h"
#include "yb/util/status_format.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_string(vmodule);
namespace yb {

static constexpr auto kTableName = "tbl1";
static constexpr auto kInt2Column = "int2_col";
static constexpr auto kInt4Column = "int4_col";
static constexpr auto kInt8Column = "int8_col";
static constexpr auto kFloat4Column = "float4_col";
static constexpr auto kFloat8Column = "float8_col";
static constexpr auto kNumericColumn = "numeric_col";
static constexpr auto kBoolColumn = "boolean_col";
static constexpr auto kNameColumn = "name_col";
static constexpr auto kTextColumn = "text_col";
static constexpr auto kPointColumn = "point_col";
static constexpr auto kBoxColumn = "box_col";
static constexpr auto kTidColumn = "tid_col";
static constexpr auto kCircleColumn = "circle_col";
static constexpr auto kCharColumn = "char_col";

static constexpr auto kStorageFilter = "Storage (Index )?Filter: ";
static constexpr auto kLocalFilter = "Filter: ";
static constexpr auto kInvalidFunctionError = "does not exist";
static constexpr auto kInvalidOperatorError =
  "No operator matches the given name and argument types";
static constexpr auto kOutofRangeError = "out of range";

static constexpr auto kExplainArgs = "(ANALYZE, COSTS OFF, TIMING OFF)";

/*
 * Behaviour Enum
 * This is used by the Expression class, to provide a behavior for PG11 nodes and a separate
 * behavior for PG15 nodes. Meanings:
 * - kPushable: The expression is pushable, but not in mixed mode.
 * - kNotPushable: The expression is not pushable.
 * - kMMPushable: The expression is pushable in mixed mode and before / after upgrade.
 * - kOperatorError: The expression is an operator error.
 * - kFunctionError: The expression is a function error.
 * - kBadCastError: The expression fails because of a bad cast, e.g. smallint out of range
 *
 * For example, setting pg11_behaviour_ to kPushable and pg15_behaviour_ to kMMPushable means that
 * the expression is:
 * - pushable in PG11
 * - not pushable in mixed mode on PG11 backends
 * - pushable in mixed mode on PG15 backends
 * - pushable in PG15
 */
YB_DEFINE_ENUM(Behaviour, (kNotPushable)(kPushable)(kMMPushable)
                          (kOperatorError)(kFunctionError)(kBadCastError));
class YsqlMajorUpgradeExpressionPushdownTest : public Pg15UpgradeTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(Pg15UpgradeTestBase);

    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    const auto create_table_stmt = Format(
        "CREATE TABLE $0($1 INT2, $2 INT4, $3 INT8, $4 FLOAT4, $5 FLOAT8, $6 NUMERIC, " \
                        "$7 NAME, $8 TEXT, $9 POINT, $10 BOX, $11 TID, $12 CIRCLE, $13 BOOL, " \
                        "$14 CHAR)",
        kTableName,
        kInt2Column, kInt4Column, kInt8Column, kFloat4Column, kFloat8Column, kNumericColumn,
        kNameColumn, kTextColumn, kPointColumn, kBoxColumn, kTidColumn, kCircleColumn, kBoolColumn,
        kCharColumn);
    LOG(INFO) << "Creating table: " << create_table_stmt;
    ASSERT_OK(conn.Execute(create_table_stmt));

    // Since we multiply int2 by int2, we need to ensure that 2^n * 2^n fits into int2 (2^15)
    for (double i = 1; i < 7; i += 0.5)
      ASSERT_OK(InsertPowerOfTwoRow(conn, i));

    CHECK_OK(SET_FLAG(vmodule, "docdb_pgapi=1"));
  }

  Status InsertPowerOfTwoRow(pgwrapper::PGConn& conn, double i) {
    auto stmt = Format("INSERT INTO $0 VALUES (" \
      "(2 ^ LEAST(15.0, $1) - 1)::smallint, " \
      "(2 ^ LEAST(31.0, $1) - 1)::int, " \
      "(2 ^ $1)::bigint, $1, $1, $1, '$1', '$1', " \
      "'($1, $1)'::point, '(1,1,1,1)'::box, ('(42, ' || round($1 % 40)::text || ')')::tid, " \
      "'<($1, $1), 1>'::circle, round($1 % 2)::int::bool, '1')",
      kTableName, i);
    LOG(INFO) << "Inserting row: " << stmt;
    return conn.Execute(stmt);
  }


  // Use these to log the state to help debug failures.
  bool mixed_mode_expression_pushdown_ = false;

  Status SetMixedModePushdownForPg15(bool value) {
    mixed_mode_expression_pushdown_ = value;

    const auto mixed_mode_flag = "ysql_yb_mixed_mode_expression_pushdown";
    const auto tserver = cluster_->tablet_server(kMixedModeTserverPg15);
    RETURN_NOT_OK(cluster_->SetFlag(tserver, mixed_mode_flag, value ? "true" : "false"));
    return Status::OK();
  }

  class Expression {
   public:
    Expression(std::string expr, Behaviour pg11, Behaviour pg15)
    : expr_(std::move(expr)), pg11_behaviour_(pg11), pg15_behaviour_(pg15) {}

    std::string expr_;

#define CREATE_CHECK_FUNCTION(type) bool Is##type(size_t ts_id) const { \
  return (ts_id == kMixedModeTserverPg15 ? pg15_behaviour_ == Behaviour::k##type \
                                         : pg11_behaviour_ == Behaviour::k##type); \
  }
    CREATE_CHECK_FUNCTION(Pushable)
    CREATE_CHECK_FUNCTION(NotPushable)
    CREATE_CHECK_FUNCTION(MMPushable)
    CREATE_CHECK_FUNCTION(OperatorError)
    CREATE_CHECK_FUNCTION(FunctionError)
    CREATE_CHECK_FUNCTION(BadCastError)

    bool IsError(size_t ts_id) const {
      if (ts_id == kMixedModeTserverPg15) {
        return pg15_behaviour_ == Behaviour::kFunctionError
            || pg15_behaviour_ == Behaviour::kOperatorError
            || pg15_behaviour_ == Behaviour::kBadCastError;
      } else {
        return pg11_behaviour_ == Behaviour::kFunctionError
            || pg11_behaviour_ == Behaviour::kOperatorError
            || pg11_behaviour_ == Behaviour::kBadCastError;
      }
    }

    Result<std::string> GetExpectedError(size_t ts_id) const {
      if (IsOperatorError(ts_id)) {
        return kInvalidOperatorError;
      } else if (IsFunctionError(ts_id)) {
        return kInvalidFunctionError;
      } else if (IsBadCastError(ts_id)) {
        return kOutofRangeError;
      }
      return STATUS_FORMAT(InternalError, "Expression $0 is not an error", expr_);
    }

   private:
    Behaviour pg11_behaviour_;
    Behaviour pg15_behaviour_;
  };

  Status TestPushdowns(const std::string& prefix,
                        const std::vector<Expression>& exprs) {
    const auto check_failure =
        [exprs, prefix](pgwrapper::PGConn& conn, const Expression& expr, size_t ts_id) -> Status {
      auto status = conn.Execute(Format("$0 $1", prefix, expr.expr_));

      SCHECK_FORMAT(!status.ok(), InternalError,
                    "Expected $0 to not be a viable PG11 expression: $1",
                    expr.expr_, status.ToString());

      auto expected_err = VERIFY_RESULT(expr.GetExpectedError(ts_id));
      SCHECK_NE(status.ToString().find(expected_err), std::string::npos, InternalError,
                Format("Expected $0 to fail with in $1: $2",
                       expr.expr_, ts_id == kMixedModeTserverPg11 ? "PG11" : "PG15",
                       status.ToString()));

      return Status::OK();
    };

    const auto check_filters = [this, prefix](pgwrapper::PGConn& conn,
                                              const std::string& filter_type,
                                              const std::string& expr,
                                              std::optional<size_t> ts_id = kAnyTserver) -> Status {
      LOG(INFO) << "Running " << Format("$0 $1", prefix, expr) << " on "
                << (ts_id == std::nullopt ? "any"
                                          : (*ts_id == kMixedModeTserverPg11 ? "pg11" : "pg15"))
                << " with mixed_mode_expression_pushdown: " << mixed_mode_expression_pushdown_;
      auto explain_result = VERIFY_RESULT(
          conn.FetchAllAsString(Format("$0 $1", prefix, expr)));
      auto lines = strings::Split(explain_result, pgwrapper::DefaultRowSeparator(),
                                  strings::SkipEmpty());
      std::string found_filter;
      for (const auto& line : lines) {
        if (regex_search(line.as_string(), std::regex("^\\s+" + filter_type))) {
          // use kLocalFilter to find the filter, because we may have matched any of
          // "Storage Filter", "Storage Index Filter", "Filter"
          found_filter = line.substr(line.find(kLocalFilter) + strlen(kLocalFilter)).as_string();
          break;
        }
      }
      SCHECK_FORMAT(!found_filter.empty(), NotFound, "$0 not found in explain result: $1",
                    filter_type, explain_result);
      SCHECK_EQ(found_filter, expr, InternalError,
                Format("Expected filter to be $0 in explain plan $1", expr, explain_result));
      return Status::OK();
    };

    const auto check = [this, &exprs, &check_filters, &check_failure](size_t ts_id) -> Status {
      auto upgrade_compat = VERIFY_RESULT(ReadUpgradeCompatibilityGuc());
      LOG(INFO) << "Running checks on "<< (ts_id == kMixedModeTserverPg11 ? "pg11" : "pg15")
                << " with mixed_mode_expression_pushdown: " << mixed_mode_expression_pushdown_
                << " and upgrade compatibility: " << upgrade_compat;
      auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
      for (auto &expr : exprs) {
        if (expr.IsError(ts_id)) {
          RETURN_NOT_OK(check_failure(conn, expr, ts_id));
        } else if (upgrade_compat == "0"
                   && (expr.IsPushable(ts_id) || expr.IsMMPushable(ts_id))) {
          RETURN_NOT_OK(check_filters(conn, kStorageFilter, expr.expr_, ts_id));
        } else if (upgrade_compat == "11" && mixed_mode_expression_pushdown_
                   && expr.IsMMPushable(ts_id)) {
          RETURN_NOT_OK(check_filters(conn, kStorageFilter, expr.expr_, ts_id));
        } else {
          RETURN_NOT_OK(check_filters(conn, kLocalFilter, expr.expr_, ts_id));
        }
      }
      return Status::OK();
    };

    // All expressions should be pushable in PG11
    RETURN_NOT_OK(check(kMixedModeTserverPg11));

    RETURN_NOT_OK(
      SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kBackwardsCompatible));
    RETURN_NOT_OK(check(kMixedModeTserverPg11));

    RETURN_NOT_OK(UpgradeClusterToMixedMode());
    RETURN_NOT_OK(check(kMixedModeTserverPg11));
    for (auto mixed_mode_expression_pushdown : {true, false}) {
      RETURN_NOT_OK(SetMixedModePushdownForPg15(mixed_mode_expression_pushdown));
      RETURN_NOT_OK(check(kMixedModeTserverPg15));
    }

    RETURN_NOT_OK(UpgradeAllTserversFromMixedMode());
    RETURN_NOT_OK(check(kMixedModeTserverPg15));

    RETURN_NOT_OK(FinalizeUpgrade());
    RETURN_NOT_OK(check(kMixedModeTserverPg15));

    return Status::OK();
  }

  const std::vector<std::string> numeric_types = {kInt2Column, kInt4Column, kInt8Column,
                                                  kFloat4Column, kFloat8Column, kNumericColumn};

  std::vector<Expression> CreateExpressions() {
    const std::vector<std::string> comparison_operations = {"=", "<>", "<", "<=", ">", ">="};
    const auto is_int( [](const std::string& type) {
      return type == kInt2Column || type == kInt4Column || type == kInt8Column;
    });

    const auto is_float( [](const std::string& type) {
      return type == kFloat4Column || type == kFloat8Column;
    });

    const auto get_cast = [](std::string &column_name) {
      if (column_name == kFloat4Column) {
        return "real";
      } else if (column_name == kFloat8Column) {
        return "double precision";
      } else if (column_name == kNumericColumn) {
        return "numeric";
      } else if (column_name == kInt2Column) {
        return "smallint";
      } else if (column_name == kInt4Column) {
        return "integer";
      } else if (column_name == kInt8Column) {
        return "bigint";
      }
      return "";
    };

    std::vector<Expression> exprs;

    // Add basic numeric comparisons
    for (auto t1 : numeric_types) {
      for (auto t2 : numeric_types) {
        for (auto op : comparison_operations) {
          if (t1 == t2 && op == "=") {
            exprs.push_back(Expression(Format("($0 IS NOT NULL)", t1),
                                      Behaviour::kPushable, Behaviour::kMMPushable));
          } else if (t1 == t2) {
            exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                      Behaviour::kPushable, Behaviour::kMMPushable));
          } else {
            // explicitly cast t2 to t1's type
            exprs.push_back(Expression(Format("($0 $1 ($2)::$3)", t1, op, t2, get_cast(t1)),
                                      Behaviour::kPushable, Behaviour::kMMPushable));

            // Float{4,8} can be compared directly with Float{4,8}
            // Int{2,4,8} can be compared directly with Int{2,4,8}
            if ((is_float(t1) && is_float(t2)) || (is_int(t1) && is_int(t2))) {
              exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                         Behaviour::kPushable, Behaviour::kMMPushable));
            }
          }
        }
      }
    }


    exprs.push_back(Expression(Format("(($0)::integer = $1)", kBoolColumn, kInt4Column),
                                      Behaviour::kPushable, Behaviour::kMMPushable));
    exprs.push_back(Expression(Format("($0 = ($1)::boolean)", kBoolColumn, kInt4Column),
                                      Behaviour::kPushable, Behaviour::kMMPushable));

    // (char_col)::integer is not pushable
    exprs.push_back(Expression(Format("(($0)::integer = $1)", kCharColumn, kInt4Column),
                                      Behaviour::kNotPushable, Behaviour::kNotPushable));
    exprs.push_back(Expression(Format("($0 = ($1)::character(1))", kCharColumn, kInt4Column),
                                      Behaviour::kNotPushable, Behaviour::kNotPushable));

    // Add basic arithmetic operations (t1 arith_op t2) = t3
    std::vector<std::string> arithmetic_ops = {"+", "*", "-", "/"};
    for (auto t1 : numeric_types) {
      for (auto t2 : numeric_types) {
        for (auto op : arithmetic_ops) {

          // arithmetic has the same type as t1
          std::string arithmetic_expr = (is_int(t1) == is_int(t2) && is_float(t1) == is_float(t2))
            ? Format("($0 $1 $2)", t1, op, t2)
            : Format("($0 $1 ($2)::$3)", t1, op, t2, get_cast(t1));

          for (auto t3 : numeric_types) {
            if (t3 == t1) {
              exprs.push_back(Expression(Format("($0 = $1)", arithmetic_expr, t3),
                                        Behaviour::kPushable, Behaviour::kMMPushable));
            } else {
              // explicitly cast t2 to t1's type
              exprs.push_back(Expression(
                Format("($0 = ($1)::$2)", arithmetic_expr, t3, get_cast(t1)),
                Behaviour::kPushable, Behaviour::kMMPushable));
            }
          }
        }
      }
    }

    // Add basic text operations
    for (auto op : comparison_operations) {
      for (auto t1 : {kCharColumn, kTextColumn}) {
        for (auto t2 : {kCharColumn, kTextColumn}) {
          if (t1 == t2) {
            if (op == "=") {
              exprs.push_back(Expression(Format("($0 IS NOT NULL)", t1, op, t2),
                                         Behaviour::kPushable, Behaviour::kMMPushable));
            } else {
              exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                         Behaviour::kPushable, Behaviour::kMMPushable));
            }
          } else if (t1 == kTextColumn) { // t2 is char
            exprs.push_back(Expression(Format("($0 $1 ($2)::text)", t1, op, t2),
                                       Behaviour::kPushable, Behaviour::kMMPushable));
            exprs.push_back(Expression(Format("(($0)::character(1) $1 $2)", t1, op, t2),
                                       Behaviour::kPushable, Behaviour::kMMPushable));
          } else { // t1 is char, t2 is text
            exprs.push_back(Expression(Format("(($0)::text $1 $2)", t1, op, t2),
                                       Behaviour::kPushable, Behaviour::kMMPushable));
            exprs.push_back(Expression(Format("($0 $1 ($2)::character(1))", t1, op, t2),
                                       Behaviour::kPushable, Behaviour::kMMPushable));
          }
        }
      }
    }

    // Add boolean operators
    std::vector<std::string> boolean_ops = {"AND", "OR"};
    for (auto op : boolean_ops) {
      exprs.push_back(Expression(Format("($0 $1 ($2 = 3))", kBoolColumn, op, kInt4Column),
                                 Behaviour::kPushable, Behaviour::kMMPushable));
    }

    for (auto op : comparison_operations) {
      exprs.push_back(Expression(Format("($0 $1 (NOT $0))", kBoolColumn, op),
                                        Behaviour::kPushable, Behaviour::kMMPushable));
    }

    // Add unary types
    for (auto t1 : numeric_types) {
      auto t2 = is_float(t1) ? "'-1'::double precision"
                             : is_int(t1) ? "'-1'::integer"
                                          : "'-1'::numeric";
      exprs.push_back(Expression(Format("((- $0) = $1)", t1, t2),
                                Behaviour::kPushable, Behaviour::kMMPushable));
      exprs.push_back(Expression(Format("(abs($0) = $1)", t1, t2),
                                Behaviour::kPushable, Behaviour::kMMPushable));
      auto col_type = t1.substr(0, t1.find("_"));
      auto opt_underscore = col_type == "numeric" ? "_" : "";
      exprs.push_back(Expression(Format("($0$1abs($2) = $3)", col_type, opt_underscore, t1, t2),
                                Behaviour::kPushable, Behaviour::kMMPushable));
    }

    exprs.push_back(Expression(Format("(NOT $0)", kBoolColumn),
                               Behaviour::kPushable, Behaviour::kMMPushable));

    return exprs;
  }
};

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestTableFilters) {
  std::vector<Expression> exprs = CreateExpressions();

  exprs.push_back(Expression(Format("($0 = '1'::text)", kTextColumn),
                             Behaviour::kPushable, Behaviour::kMMPushable));

  for (const auto &expr : {
      Format("(($0 <-> '(1,1)'::point) < '1'::double precision)", kPointColumn),
      Format("CASE WHEN ($0 = 1) THEN true ELSE false END", kInt4Column),
      Format("($0 = ANY ('{1,2,3,4}'::integer[]))", kInt4Column),
      Format("($0 = '1'::name)", kNameColumn)
  }) {
    exprs.push_back(Expression(expr, Behaviour::kPushable, Behaviour::kPushable));
  }

  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTableName),
                          exprs));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestOutOfBoundsConversions) {
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    for (int i = 10; i < 40; i++) {
      ASSERT_OK(InsertPowerOfTwoRow(conn, i));
    }
  }

  const auto get_cast = [](std::string &column_name) {
    if (column_name == kInt2Column) {
      return "smallint";
    } else if (column_name == kInt4Column) {
      return "integer";
    } else if (column_name == kInt8Column) {
      return "bigint";
    }
    return "";
  };

  std::vector<Expression> exprs;
  std::vector<std::string> int_cols = {kInt2Column, kInt4Column, kInt8Column};
  for (auto &t1 : int_cols) {
    for (auto &t2 : int_cols) {
      if (t1 == t2) // comparing the same col results in IS NOT NULL
        continue;

      // no explicit casting is fine because they are implicitly upcasted
      exprs.push_back(Expression(Format("($0 = $1)", t1, t2),
                                Behaviour::kPushable, Behaviour::kMMPushable));

      auto cond = Format("($0 = ($1)::$2)", t1, t2, get_cast(t1));
      if (t1 < t2) {
        // casting t2 to a smaller type will cause an error
        exprs.push_back(Expression(cond, Behaviour::kBadCastError, Behaviour::kBadCastError));
      } else {
        // casting t2 to a larger type is fine
        exprs.push_back(Expression(cond, Behaviour::kPushable, Behaviour::kMMPushable));
      }
    }
  }

  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTableName),
                          exprs));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestIndexFilters) {
  const auto kIndexName = "idx";

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 ($2 ASC) " \
        "INCLUDE ($3, $4, $5, $6, $7, $8, $9, $10, $11, $12)",
        kIndexName, kTableName, kNameColumn,
        // Included Cols
        kInt2Column, kInt4Column, kInt8Column, kFloat4Column, kFloat8Column,
        kTextColumn, kPointColumn, kBoxColumn, kTidColumn, kBoolColumn));
  }

  const auto prefix = Format(
        "/* IndexScan($0) */ EXPLAIN $1 SELECT * FROM $2 WHERE $3 < '9' AND ",
        kIndexName, kExplainArgs, kTableName, kNameColumn);

  std::vector<Expression> exprs = CreateExpressions();

  for (const auto &expr : {
      Format("(($0 <-> '(1,1)'::point) < '1'::double precision)", kPointColumn),
      Format("CASE WHEN ($0 = 1) THEN true ELSE false END", kInt4Column),
      Format("($0 = ANY ('{1,2,3,4}'::integer[]))", kInt4Column),
  }) {
    exprs.push_back(Expression(expr, Behaviour::kPushable, Behaviour::kPushable));
  }

  ASSERT_OK(TestPushdowns(prefix, exprs));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestSystemTables) {
  const auto prefix = Format(
      "/*+ Set(enable_indexscan off) */ EXPLAIN $0 SELECT * FROM pg_class WHERE",
      kExplainArgs);

  ASSERT_OK(TestPushdowns(prefix, {
    Expression("(relname = 'pg_proc'::name)", Behaviour::kPushable, Behaviour::kPushable),
    Expression("(relowner = '10'::oid)", Behaviour::kPushable, Behaviour::kPushable),
    Expression("(relkind = 'r'::\"char\")", Behaviour::kPushable, Behaviour::kPushable),
  }));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestNewPg15Functions) {
  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTableName), {
    Expression(Format("(('[(1,1),(1,1)]'::lseg <-> $0) < '1'::double precision)", kBoxColumn),
               Behaviour::kPushable, Behaviour::kPushable), // lseg <-> box existed in PG11
    Expression(Format("(($0 <-> '[(1,1),(1,1)]'::lseg) < '1'::double precision)", kBoxColumn),
               Behaviour::kFunctionError, Behaviour::kPushable), // box <-> lseg was added in PG15
    Expression(Format("(hashtid($0) = 1)", kTidColumn),
               Behaviour::kFunctionError, Behaviour::kPushable),
    Expression(Format("(log10(($0)::double precision) < '10'::double precision)", kInt4Column),
               Behaviour::kFunctionError, Behaviour::kPushable),
    Expression(Format("($0 |>> '(1,1)'::point)", kPointColumn),
               Behaviour::kOperatorError, Behaviour::kPushable),
    Expression(Format("($0 <<| '(1,1)'::point)", kPointColumn),
               Behaviour::kOperatorError, Behaviour::kPushable),
    Expression(Format("(('((1,0),(1,1),(1,1))'::polygon <-> $0) < '10'::double precision)",
                      kCircleColumn),
               Behaviour::kOperatorError, Behaviour::kPushable),
  }));
}

}  // namespace yb
