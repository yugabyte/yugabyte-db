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
#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

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
static constexpr auto kUuidColumn = "uuid_col";

static constexpr auto kStorageFilter = "Storage (Index )?Filter: ";
static constexpr auto kLocalFilter = "Filter: ";

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
 * - kOutOfRangeError: The expression fails because of a bad cast, e.g. smallint out of range
 * - kBadCastError: The expression fails because the two types are incompatible
 *
 * For example, setting pg11_behaviour_ to kPushable and pg15_behaviour_ to kMMPushable means that
 * the expression is:
 * - pushable in PG11
 * - not pushable in mixed mode on PG11 backends
 * - pushable in mixed mode on PG15 backends
 * - pushable in PG15
 */
YB_DEFINE_ENUM(Behaviour, (kNotPushable)(kPushable)(kMMPushable)
                          (kOperatorError)(kFunctionError)(kOutOfRangeError)(kBadCastError));
class YsqlMajorUpgradeExpressionPushdownTest : public YsqlMajorUpgradeTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(YsqlMajorUpgradeTestBase);

    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    const auto create_table_stmt = Format(
        "CREATE TABLE $0($1 INT2, $2 INT4, $3 INT8, $4 FLOAT4, $5 FLOAT8, $6 NUMERIC, " \
                        "$7 NAME, $8 TEXT, $9 POINT, $10 BOX, $11 TID, $12 CIRCLE, $13 BOOL, " \
                        "$14 CHAR, $15 UUID)",
        kTableName,
        kInt2Column, kInt4Column, kInt8Column, kFloat4Column, kFloat8Column, kNumericColumn,
        kNameColumn, kTextColumn, kPointColumn, kBoxColumn, kTidColumn, kCircleColumn, kBoolColumn,
        kCharColumn, kUuidColumn);
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
      "'<($1, $1), 1>'::circle, round($1 % 2)::int::bool, '1', " \
      "('12345679-1234-5678-1234-' || lpad(round($1)::text, 12, '0'))::uuid)",
      kTableName, i);
    LOG(INFO) << "Inserting row: " << stmt;
    return conn.Execute(stmt);
  }

  bool mixed_mode_expression_pushdown_ = false;
  bool mixed_mode_saop_pushdown_ = false;
  bool mixed_mode_ = false;

  Status SetMixedModeSaopPushdownForPg15(bool value) {
    if (!mixed_mode_)
      // We can't set this flag on PG11 servers, so first validate that there is a PG15 server
      return Status::OK();

    mixed_mode_saop_pushdown_ = value;

    const auto saop_flag = "ysql_yb_mixed_mode_saop_pushdown";
    const auto tserver = cluster_->tablet_server(kMixedModeTserverPg15);
    RETURN_NOT_OK(cluster_->SetFlag(tserver, saop_flag, value ? "true" : "false"));
    std::this_thread::sleep_for(50ms); // Sometimes the flag takes a while to propagate
    return Status::OK();
  }

  Status SetMixedModePushdown(bool value) {
    mixed_mode_expression_pushdown_ = value;
    const auto mixed_mode_flag = "ysql_yb_mixed_mode_expression_pushdown";
    RETURN_NOT_OK(cluster_->SetFlagOnTServers(mixed_mode_flag, value ? "true" : "false"));
    return Status::OK();
  }

  /*
   * Expression is a class designed to hold a condition and its expected behaviour in PG11 and PG15.
   * There's an optional parameter match_regex that can be used to match the filter in the explain
   * plan, for cases where the filter is difficult to get to exactly match the expression. If
   * match_regex is unspecified, the filter in the explain plan must exactly match the expression.
   */
  class Expression {
   public:
    Expression(std::string expr, Behaviour pg11, Behaviour pg15,
               std::optional<std::string> match_regex = std::nullopt)
    : expr_(std::move(expr)), match_regex_(match_regex),
      pg11_behaviour_(pg11), pg15_behaviour_(pg15) {}

    /* Constructor for cases where the pg11 and pg15 behaviour are the same */
    Expression(std::string expr, Behaviour pg11_pg15_behaviour,
               std::optional<std::string> match_regex = std::nullopt)
    : expr_(std::move(expr)), match_regex_(match_regex),
      pg11_behaviour_(pg11_pg15_behaviour), pg15_behaviour_(pg11_pg15_behaviour) {}

    std::string expr_;
    std::optional<std::string> match_regex_;

#define CREATE_CHECK_FUNCTION(type) bool Is##type(size_t ts_id) const { \
  return (ts_id == kMixedModeTserverPg15 ? pg15_behaviour_ == Behaviour::k##type \
                                         : pg11_behaviour_ == Behaviour::k##type); \
  }
    CREATE_CHECK_FUNCTION(Pushable)
    CREATE_CHECK_FUNCTION(NotPushable)
    CREATE_CHECK_FUNCTION(MMPushable)
    CREATE_CHECK_FUNCTION(OperatorError)
    CREATE_CHECK_FUNCTION(FunctionError)
    CREATE_CHECK_FUNCTION(OutOfRangeError)
    CREATE_CHECK_FUNCTION(BadCastError)

    bool IsError(size_t ts_id) const {
      if (ts_id == kMixedModeTserverPg15) {
        return pg15_behaviour_ == Behaviour::kFunctionError
            || pg15_behaviour_ == Behaviour::kOperatorError
            || pg15_behaviour_ == Behaviour::kOutOfRangeError
            || pg15_behaviour_ == Behaviour::kBadCastError;
      } else {
        return pg11_behaviour_ == Behaviour::kFunctionError
            || pg11_behaviour_ == Behaviour::kOperatorError
            || pg11_behaviour_ == Behaviour::kOutOfRangeError
            || pg11_behaviour_ == Behaviour::kBadCastError;
      }
    }

    Result<std::string> GetExpectedError(size_t ts_id) const {
      if (IsOperatorError(ts_id)) {
        return "No operator matches the given name and argument types";
      } else if (IsFunctionError(ts_id)) {
        return "does not exist";
      } else if (IsOutOfRangeError(ts_id)) {
        return "out of range";
      } else if (IsBadCastError(ts_id)) {
        return "cannot cast type";
      }
      return STATUS_FORMAT(InternalError, "Expression $0 is not an error", expr_);
    }

    bool IsSaopExpression() const {
      return expr_.find("ANY") != std::string::npos || expr_.find("ALL") != std::string::npos;
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
                                              const Expression& expr,
                                              std::optional<size_t> ts_id = kAnyTserver) -> Status {
      LOG(INFO) << "Running " << Format("$0 $1", prefix, expr.expr_) << " on "
                << (ts_id == std::nullopt ? "any"
                                          : (*ts_id == kMixedModeTserverPg11 ? "pg11" : "pg15"))
                << " with mixed_mode_expression_pushdown: " << mixed_mode_expression_pushdown_
                << " and mixed_mode_saop_pushdown: " << mixed_mode_saop_pushdown_;
      auto explain_result = VERIFY_RESULT(
          conn.FetchAllAsString(Format("$0 $1", prefix, expr.expr_)));
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

      if (expr.match_regex_.has_value())
        SCHECK(regex_search(found_filter, std::regex(expr.match_regex_.value())) , InternalError,
                  Format("Expected filter to match $0 in explain plan $1",
                         expr.match_regex_.value(), explain_result));
      else
        SCHECK_EQ(found_filter, expr.expr_, InternalError,
                  Format("Expected filter to equal $0 in explain plan $1",
                         expr.expr_, explain_result));
      return Status::OK();
    };

    const auto check = [this, &exprs, &check_filters, &check_failure](size_t ts_id) -> Status {
      auto upgrade_compat = VERIFY_RESULT(ReadUpgradeCompatibilityGuc());
      LOG(INFO) << "Running checks on " << (ts_id == kMixedModeTserverPg11 ? "pg11" : "pg15")
                << " with mixed_mode_expression_pushdown: " << mixed_mode_expression_pushdown_
                << " and upgrade compatibility: " << upgrade_compat;
      auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
      for (auto &expr : exprs) {
        if (expr.IsError(ts_id)) {
          RETURN_NOT_OK(check_failure(conn, expr, ts_id));
        } else if (upgrade_compat == "0"
                   && (expr.IsPushable(ts_id) || expr.IsMMPushable(ts_id))) {
          RETURN_NOT_OK(check_filters(conn, kStorageFilter, expr, ts_id));
        } else if (upgrade_compat == "11" && mixed_mode_expression_pushdown_
                   && expr.IsMMPushable(ts_id)) {
          RETURN_NOT_OK(check_filters(conn, kStorageFilter, expr, ts_id));
        } else {
          RETURN_NOT_OK(check_filters(conn, kLocalFilter, expr, ts_id));
        }
      }

      if (upgrade_compat == "11" && mixed_mode_expression_pushdown_) {
        /* Retry any SAOP expressions with SAOP pushdown disabled */
        RETURN_NOT_OK(SetMixedModeSaopPushdownForPg15(false));
        for (auto &expr : exprs) {
          if (expr.IsSaopExpression()) {
            RETURN_NOT_OK(check_filters(conn, kLocalFilter, expr, ts_id));
          }
        }
        RETURN_NOT_OK(SetMixedModeSaopPushdownForPg15(true));
      }

      return Status::OK();
    };

    // All expressions should be pushable in PG11
    mixed_mode_ = false;
    RETURN_NOT_OK(check(kMixedModeTserverPg11));

    RETURN_NOT_OK(
      SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kBackwardsCompatible));

    for (auto mixed_mode_expression_pushdown : {true, false}) {
      RETURN_NOT_OK(SetMixedModePushdown(mixed_mode_expression_pushdown));
      RETURN_NOT_OK(check(kMixedModeTserverPg11));
    }

    RETURN_NOT_OK(UpgradeClusterToMixedMode());
    mixed_mode_ = true;

    RETURN_NOT_OK(SetMixedModeSaopPushdownForPg15(true));
    for (auto mixed_mode_expression_pushdown : {true, false}) {
      RETURN_NOT_OK(SetMixedModePushdown(mixed_mode_expression_pushdown));
      RETURN_NOT_OK(check(kMixedModeTserverPg11));
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
                                       Behaviour::kMMPushable));
          } else if (t1 == t2) {
            exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                       Behaviour::kMMPushable));
          } else {
            // explicitly cast t2 to t1's type
            exprs.push_back(Expression(Format("($0 $1 ($2)::$3)", t1, op, t2, get_cast(t1)),
                                       Behaviour::kMMPushable));

            // Float{4,8} can be compared directly with Float{4,8}
            // Int{2,4,8} can be compared directly with Int{2,4,8}
            if ((is_float(t1) && is_float(t2)) || (is_int(t1) && is_int(t2))) {
              exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                         Behaviour::kMMPushable));
            }
          }
        }
      }
    }

    const auto get_mod_name = [](std::string &col) {
      if (col == kInt2Column) {
        return "int2mod";
      } else if (col == kInt4Column) {
        return "int4mod";
      } else if (col == kInt8Column) {
        return "int8mod";
      } else if (col == kNumericColumn) {
        return "numeric_mod";
      }
      return "invalid mod function";
    };

    // Add MOD expressions
    for (auto t1 : numeric_types) {
      if (is_float(t1))
        continue;
      if (t1 == kInt4Column) {
        for (const auto& mod_name : {get_mod_name(t1), "mod"}) {
          exprs.push_back(Expression(Format("($0($1, 10) = 0)", mod_name, t1),
                                     Behaviour::kPushable, Behaviour::kMMPushable));
        }
        exprs.push_back(Expression(Format("(($0 % 10) = 0)", t1),
                                   Behaviour::kPushable, Behaviour::kMMPushable));
      } else if (is_int(t1)) {
        for (const auto& mod_name : {get_mod_name(t1), "mod"}) {
          exprs.push_back(Expression(Format("($0($1, '10'::$2) = 0)", mod_name, t1, get_cast(t1)),
                                     Behaviour::kPushable, Behaviour::kMMPushable));
        }
        exprs.push_back(Expression(Format("(($0 % '10'::$1) = 0)", t1, get_cast(t1)),
                                   Behaviour::kPushable, Behaviour::kMMPushable));
      } else {
        for (const auto &mod_name : {"numeric_mod", "mod"}) {
          exprs.push_back(Expression(Format("($0($1, '10'::numeric) = 0.0)", mod_name, t1),
                                     Behaviour::kPushable, Behaviour::kMMPushable));
        }
        exprs.push_back(Expression(Format("(($0 % '10'::numeric) = 0.0)", t1),
                                   Behaviour::kPushable, Behaviour::kMMPushable));
      }
    }

    exprs.push_back(Expression(Format("(($0)::integer = $1)", kBoolColumn, kInt4Column),
                               Behaviour::kMMPushable));
    exprs.push_back(Expression(Format("($0 = ($1)::boolean)", kBoolColumn, kInt4Column),
                               Behaviour::kMMPushable));

    // (char_col)::integer is not pushable
    exprs.push_back(Expression(Format("(($0)::integer = $1)", kCharColumn, kInt4Column),
                               Behaviour::kNotPushable));
    exprs.push_back(Expression(Format("($0 = ($1)::character(1))", kCharColumn, kInt4Column),
                               Behaviour::kNotPushable));

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
                                         Behaviour::kMMPushable));
            } else {
              // explicitly cast t2 to t1's type
              exprs.push_back(Expression(
                Format("($0 = ($1)::$2)", arithmetic_expr, t3, get_cast(t1)),
                Behaviour::kMMPushable));
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
                                         Behaviour::kMMPushable));
            } else {
              exprs.push_back(Expression(Format("($0 $1 $2)", t1, op, t2),
                                         Behaviour::kMMPushable));
            }
          } else if (t1 == kTextColumn) { // t2 is char
            exprs.push_back(Expression(Format("($0 $1 ($2)::text)", t1, op, t2),
                                       Behaviour::kMMPushable));
            exprs.push_back(Expression(Format("(($0)::character(1) $1 $2)", t1, op, t2),
                                       Behaviour::kMMPushable));
          } else { // t1 is char, t2 is text
            exprs.push_back(Expression(Format("(($0)::text $1 $2)", t1, op, t2),
                                       Behaviour::kMMPushable));
            exprs.push_back(Expression(Format("($0 $1 ($2)::character(1))", t1, op, t2),
                                       Behaviour::kMMPushable));
          }
        }
      }
    }

    // Add boolean operators
    std::vector<std::string> boolean_ops = {"AND", "OR"};
    for (auto op : boolean_ops) {
      exprs.push_back(Expression(Format("($0 $1 ($2 = 3))", kBoolColumn, op, kInt4Column),
                                 Behaviour::kMMPushable));
    }

    for (auto op : comparison_operations) {
      exprs.push_back(Expression(Format("($0 $1 (NOT $0))", kBoolColumn, op),
                                 Behaviour::kMMPushable));
    }

    // Add unary types
    for (auto t1 : numeric_types) {
      auto t2 = is_float(t1) ? "'-1'::double precision"
                             : is_int(t1) ? "'-1'::integer"
                                          : "'-1'::numeric";
      exprs.push_back(Expression(Format("((- $0) = $1)", t1, t2),
                                 Behaviour::kMMPushable));
      exprs.push_back(Expression(Format("(abs($0) = $1)", t1, t2),
                                 Behaviour::kMMPushable));
      auto col_type = t1.substr(0, t1.find("_"));
      auto opt_underscore = col_type == "numeric" ? "_" : "";
      exprs.push_back(Expression(Format("($0$1abs($2) = $3)", col_type, opt_underscore, t1, t2),
                                 Behaviour::kMMPushable));
    }

    exprs.push_back(Expression(Format("(NOT $0)", kBoolColumn),
                               Behaviour::kMMPushable));

    // Add UUID comparisons
    for (auto op : comparison_operations) {
      exprs.push_back(Expression(Format("($0 $1 '12345679-1234-5678-1234-123456789012'::uuid)",
                                       kUuidColumn, op),
                                 Behaviour::kMMPushable));
    }

    return exprs;
  }
};

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestScalarArrayOpExprs) {
  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTableName), {
    Expression(Format("($0 = ANY ('{1,2}'::integer[]))", kInt4Column),
               Behaviour::kPushable, Behaviour::kMMPushable),
    Expression(Format("($0 <> ALL ('{1,2}'::text[]))", kTextColumn),
               Behaviour::kPushable, Behaviour::kMMPushable),
    Expression(Format("(($0 = ANY ('{1,2}'::integer[])) AND ($1 = ANY ('{1,2}'::text[])))",
                      kInt4Column, kTextColumn),
               Behaviour::kPushable, Behaviour::kMMPushable),
    Expression(Format("(($0 = ANY ('{1,2}'::integer[])) OR ($1 = ANY ('{1,2}'::text[])))",
                      kInt4Column, kTextColumn),
               Behaviour::kPushable, Behaviour::kMMPushable),
    Expression(Format("($0 ~~ ANY ('{1%,2%}'::text[]))", kTextColumn),
               Behaviour::kPushable, Behaviour::kMMPushable),
  }));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestTableFilters) {
  std::vector<Expression> exprs = CreateExpressions();

  exprs.push_back(Expression(Format("($0 = '1'::text)", kTextColumn),
                             Behaviour::kMMPushable));

  for (const auto &expr : {
      Format("(($0 <-> '(1,1)'::point) < '1'::double precision)", kPointColumn),
      Format("CASE WHEN ($0 = 1) THEN true ELSE false END", kInt4Column),
      Format("($0 = '1'::name)", kNameColumn)
  }) {
    exprs.push_back(Expression(expr, Behaviour::kPushable));
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
                                Behaviour::kMMPushable));

      auto cond = Format("($0 = ($1)::$2)", t1, t2, get_cast(t1));
      if (t1 < t2) {
        // casting t2 to a smaller type will cause an error
        exprs.push_back(Expression(cond, Behaviour::kOutOfRangeError));
      } else {
        // casting t2 to a larger type is fine
        exprs.push_back(Expression(cond, Behaviour::kMMPushable));
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
  }) {
    exprs.push_back(Expression(expr, Behaviour::kPushable));
  }

  ASSERT_OK(TestPushdowns(prefix, exprs));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestSystemTables) {
  const auto prefix = Format(
      "/*+ Set(enable_indexscan off) */ EXPLAIN $0 SELECT * FROM pg_class WHERE",
      kExplainArgs);

  ASSERT_OK(TestPushdowns(prefix, {
    Expression("(relname = 'pg_proc'::name)", Behaviour::kPushable),
    Expression("(relowner = '10'::oid)", Behaviour::kPushable),
    Expression("(relkind = 'r'::\"char\")", Behaviour::kPushable),
  }));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestNewPg15Functions) {
  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTableName), {
    Expression(Format("(('[(1,1),(1,1)]'::lseg <-> $0) < '1'::double precision)", kBoxColumn),
               Behaviour::kPushable), // lseg <-> box existed in PG11
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

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestStringOperations) {
  static const auto kTable = "text_tbl";
  static const auto kTextColumn = "text_col";
  static const auto kVarcharColumn = "varchar_col";
  static const auto kCharColumn = "char_col";
  static const auto kBpCharColumn = "bpchar_col";

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0($1 TEXT, $2 VARCHAR, $3 CHAR, $4 BPCHAR)",
        kTable, kTextColumn, kVarcharColumn, kCharColumn, kBpCharColumn));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES ('hello', 'world', 'a', 'b')", kTable));
  }

  std::vector<Expression> exprs;
  for (const auto &cond : {
    Format("($0 ~~ 'h%'::text)", kTextColumn), // F_TEXTLIKE
    Format("($0 !~~ 'h%'::text)", kTextColumn), // F_TEXTNLIKE
    Format("($0 ~~ ($1)::text)", kTextColumn, kVarcharColumn), // F_LIKE_TEXT_TEXT
    Format("($0 !~~ ($1)::text)", kTextColumn, kVarcharColumn), // F_NOTLIKE_TEXT_TEXT
    Format("($0 ~~ 'b%'::text)", kBpCharColumn), // F_BPCHARLIKE
    Format("($0 !~~ 'b%'::text)", kBpCharColumn), // F_BPCHARNLIKE
    Format("($0 ~~* 'H%'::text)", kTextColumn), // F_TEXTICLIKE
    Format("($0 !~~* 'H%'::text)", kTextColumn), // F_TEXTICNLIKE
    Format("($0 ~~* 'B%'::text)", kBpCharColumn), // F_BPCHARICLIKE
    Format("($0 !~~* 'B%'::text)", kBpCharColumn), // F_BPCHARICNLIKE
    Format("($0 ~ 'h.*'::text)", kTextColumn), // F_TEXTREGEXEQ
    Format("($0 !~ 'h.*'::text)", kTextColumn), // F_TEXTREGEXNE
    Format("(ascii($0) = 104)", kTextColumn), // F_ASCII
    Format("(\"substring\"($0, 'h.'::text) = 'he'::text)", kTextColumn), // F_SUBSTRING_TEXT_TEXT
    Format("(\"substring\"($0, 2) = 'ello'::text)", kTextColumn), // F_SUBSTRING_TEXT_INT4
    Format("(\"substring\"($0, 2, 3) = 'ell'::text)", kTextColumn), // F_SUBSTRING_TEXT_INT4_INT4
  }) {
    exprs.push_back(Expression(cond, Behaviour::kPushable, Behaviour::kMMPushable));
  }

  // special cases: the formatting for these conditions changes in the output depending on the YSQL
  // version, so use a regex to check that the correct condition is present in the output
  exprs.push_back(Expression(Format("like($0, 'h%'::text)", kTextColumn), // F_LIKE_TEXT_TEXT
                             Behaviour::kPushable, Behaviour::kMMPushable, ".*like.*"));
  exprs.push_back(Expression(Format("notlike($0, 'h%'::text)", kTextColumn), // F_NOTLIKE_TEXT_TEXT
                             Behaviour::kPushable, Behaviour::kMMPushable, ".*notlike.*"));

  // these functions don't exist in PG11, so they can't be pushed
  exprs.push_back(Expression(Format("regexp_like($0, 'h.*'::text)", kTextColumn),
                             Behaviour::kFunctionError, Behaviour::kPushable, ".*regexp_like.*"));

  for (const auto& cond : {
    Format("(regexp_substr($0, 'h.'::text) = 'he'::text)", kTextColumn),
    Format("(regexp_substr($0, 'h.'::text, 1) = 'he'::text)", kTextColumn),
  }) {
    exprs.push_back(Expression(cond, Behaviour::kFunctionError, Behaviour::kPushable));
  }

  const auto prefix = Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTable);
  ASSERT_OK(TestPushdowns(prefix, exprs));
}

TEST_F(YsqlMajorUpgradeExpressionPushdownTest, TestTimePushdowns) {
  static const auto kTimeTable = "time_tbl";
  static const auto kTimestampCol = "timestamp_col";
  static const auto kTimestamptzCol = "timestamptz_col";
  static const auto kDateCol = "date_col";
  static const auto kTimeCol = "time_col";
  static const auto kTimetzCol = "timetz_col";
  static const auto kIntervalCol = "interval_col";

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 ($1 timestamp, $2 timestamptz, $3 date, $4 time, $5 timetz, $6 interval)",
        kTimeTable, kTimestampCol, kTimestamptzCol, kDateCol, kTimeCol, kTimetzCol, kIntervalCol));
    for (int i = 1; i <= 12; i++) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0 VALUES ('2022-$1-$1 00:00:00', '2022-$1-$1 00:00:00+00', " \
          "'2022-$1-$1', '$1:00:00', '$1:00:00+00', '$1 days')",
          kTimeTable, i));
    }
  }

  const auto get_cast = [](const std::string& col) {
    if (col == kTimestampCol) {
        return "timestamp without time zone";
    } else if (col == kTimestamptzCol) {
        return "timestamp with time zone";
    } else if (col == kDateCol) {
        return "date";
    } else if (col == kTimeCol) {
        return "time without time zone";
    } else if (col == kTimetzCol) {
        return "time with time zone";
    } else if (col == kIntervalCol) {
        return "interval";
    }
    return "";
  };

  const std::vector<std::string> time_cols =
      {kTimestampCol, kTimestamptzCol, kDateCol, kTimeCol, kTimetzCol};

  std::vector<Expression> exprs;
  for (const auto &op : {"=", "<>", "<", "<=", ">", ">="}) {
    exprs.push_back(Expression(Format("($0 $1 '1 day'::interval)", kIntervalCol, op),
                               Behaviour::kMMPushable));

    for (const auto& col : time_cols) {
      exprs.push_back(Expression(
          Format("($0 $1 '2022-01-01 00:00:00'::$2)", col, op, get_cast(col)),
          Behaviour::kMMPushable,
          // The timezone is modified to use the server timezone, so omit the actual time value.
          std::optional(Format("$0 $1 .*::$2", col, op, get_cast(col)))));
    }
  }

  const auto has_date = [](const std::string& col) {
    return col == kDateCol || col == kTimestampCol || col == kTimestamptzCol;
  };
  const auto has_tz = [](const std::string& col) {
    return col == kTimestamptzCol || col == kTimetzCol;
  };

  // Try casting between all time types. Most must be done locally, but there are some exceptions.
  for (const auto& from : time_cols) {
    for (const auto& to : time_cols) {
      if (from == to)
        continue;

      Behaviour behaviour = Behaviour::kNotPushable;
      if (!has_tz(to) && from == kTimestampCol)
        behaviour = Behaviour::kPushable;
      else if (!has_date(to) && from == kDateCol)
        behaviour = Behaviour::kBadCastError;
      else if (has_date(to) && !has_date(from))
        behaviour = Behaviour::kBadCastError;
      else if (to == kTimeCol && from == kTimetzCol)
        behaviour = Behaviour::kPushable;
      else if (to == kTimestampCol && from == kDateCol)
        behaviour = Behaviour::kPushable;
      else if (to == kTimetzCol && from == kTimestampCol)
        behaviour = Behaviour::kBadCastError;

      exprs.push_back(Expression(Format("($0 = ($1)::$2)", to, from, get_cast(to)),
                                 behaviour, behaviour));
    }
  }

  ASSERT_OK(TestPushdowns(Format("EXPLAIN $0 SELECT * FROM $1 WHERE", kExplainArgs, kTimeTable),
                          exprs));
}

}  // namespace yb
