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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import com.google.common.collect.Sets;
import com.google.common.net.HostAndPort;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.ProcessUtil;
import org.yb.util.SideBySideDiff;
import org.yb.util.StringUtil;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPushdownCompatibilityMode extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlDump.class);

  private static final String kTableName = "tbl1";
  private static final String kInt2Column = "int2_col";
  private static final String kInt4Column = "int4_col";
  private static final String kInt8Column = "int8_col";
  private static final String kFloat4Column = "float4_col";
  private static final String kFloat8Column = "float8_col";
  private static final String kNumericColumn = "numeric_col";
  private static final String kBoolColumn = "boolean_col";
  private static final String kNameColumn = "name_col";
  private static final String kTextColumn = "text_col";
  private static final String kPointColumn = "point_col";
  private static final String kBoxColumn = "box_col";
  private static final String kTidColumn = "tid_col";
  private static final String kCircleColumn = "circle_col";
  private static final String kCharColumn = "char_col";
  private static final String kUuidColumn = "uuid_col";

  private static final String kTimestampCol = "timestamp_col";
  private static final String kTimestamptzCol = "timestamptz_col";
  private static final String kDateCol = "date_col";
  private static final String kTimeCol = "time_col";
  private static final String kTimetzCol = "timetz_col";
  private static final String kIntervalCol = "interval_col";

  private static final Pattern kStorageFilter = Pattern.compile("^[\t ]+Storage (Index )?Filter: ");
  private static final Pattern kLocalFilter = Pattern.compile("^[\t ]+Filter: ");

  private static final String kExplainArgs = "(ANALYZE, COSTS OFF, TIMING OFF)";

  private static enum Behaviour {
    kNotPushable, // The expression is not pushable
    kPushable, // The expression is pushable but not in compatibility mode
    kMMPushable, // The expression is pushable in compatibility mode
    kOperatorError, // The expression is a operator error.
    kFunctionError, // The expression is a function error.
    kOutofRangeError, // The expression fails because of a bad cast, e.g. smallint out of range
    kBadCastError, // The expression fails because the two types are incompatible
  }

  @Before
  public void setUp() throws Exception {
    Statement statement = connection.createStatement();
    String createTable = String.format(
      "CREATE TABLE %s(%s INT2, %s INT4, %s INT8, %s FLOAT4, %s FLOAT8, %s NUMERIC, %s NAME, " +
      "%s TEXT, %s POINT, %s BOX, %s TID, %s CIRCLE, %s BOOL, %s CHAR, %s UUID)",
      kTableName,
      kInt2Column,
      kInt4Column,
      kInt8Column,
      kFloat4Column,
      kFloat8Column,
      kNumericColumn,
      kNameColumn,
      kTextColumn,
      kPointColumn,
      kBoxColumn,
      kTidColumn,
      kCircleColumn,
      kBoolColumn,
      kCharColumn,
      kUuidColumn
    );
    LOG.info("Creating table: " + createTable);
    statement.execute(createTable);

    // Since we multiply int2 by int2, we need to ensure that 2^n * 2^n fits into int2 (2^15)
    for (double i = 1; i < 7; i += 0.5) InsertPowerOfTwoRow(statement, i);
  }

  private void InsertPowerOfTwoRow(Statement statement, double i) throws Exception {
    String insert = String.format(
      "INSERT INTO %s VALUES (" +
      "(2 ^ LEAST(15.0, %f) - 1)::smallint, " +
      "(2 ^ LEAST(31.0, %f) - 1)::int, " +
      "(2 ^ %f)::bigint, %f, %f, %f, '%f', '%f', " +
      "'(%f, %f)'::point, '(1,1,1,1)'::box, ('(42, ' || round(%f %% 40)::text || ')')::tid, " +
      "'<(%f, %f), 1>'::circle, round(%f %% 2)::int::bool, '1', " +
      "('12345679-1234-5678-1234-' || lpad(round(%f)::text, 12, '0'))::uuid)",
      kTableName,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i,
      i
    );
    LOG.info(insert);
    statement.execute(insert);
  }

  /*
   * Expression is a class designed to hold a condition and its expected behaviour.
   * There's an optional parameter match_regex that can be used to match the filter in the explain
   * plan, for cases where the filter is difficult to get to exactly match the expression. If
   * match_regex is unspecified, the filter in the explain plan must exactly match the expression.
   */
  class Expression {

    String expr;
    Behaviour behaviour;
    Pattern match_regex;

    Expression(String expr, Behaviour behaviour, Pattern match_regex) {
      this.expr = expr;
      this.behaviour = behaviour;
      this.match_regex = match_regex;
    }

    Expression(String expr, Behaviour behaviour) {
      this(expr, behaviour, null);
    }

    boolean isSaopExpression() {
      return expr.contains("ANY") || expr.contains("ALL");
    }

    String getExpectedError() {
      if (behaviour == Behaviour.kOperatorError) {
        return "No operator matches the given name and argument types";
      } else if (behaviour == Behaviour.kFunctionError) {
        return "does not exist";
      } else if (behaviour == Behaviour.kOutofRangeError) {
        return "out of range";
      } else if (behaviour == Behaviour.kBadCastError) {
        return "cannot cast type";
      }
      fail("Unknown behaviour: " + behaviour);
      return null;
    }
  }

  private void SetCompatibilityMode(String version) throws Exception {
    Set<HostAndPort> tServers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tServer : tServers) {
      LOG.info(String.format("Setting compatibility mode to %s for %s", version, tServer));
      setServerFlag(tServer, "ysql_yb_major_version_upgrade_compatibility", version);
    }
  }

  private void SetMixedModeSaopPushdown(String value) throws Exception {
    Set<HostAndPort> tServers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tServer : tServers) {
      LOG.info(String.format("Setting saop pushdown to %s for %s", value, tServer));
      setServerFlag(tServer, "ysql_yb_mixed_mode_saop_pushdown", value);
    }
  }

  private void TestPushdowns(String prefix, List<Expression> expressions) throws Exception {
    SetMixedModeSaopPushdown("true");
    CheckExprs(prefix, expressions, "0");
    CheckExprs(prefix, expressions, "11");
  }

  /* CheckFailure
   *
   * Checks for expected failures of a given Expression, and fails if no failure
   * is found or if the failure doesn't match the Expression's expected failure
   * mode.
   */
  private void CheckFailure(String prefix, Expression expr) {
    try {
      Statement statement = connection.createStatement();
      statement.execute(String.format("%s %s", prefix, expr.expr));
      fail("Expected query to fail: " + expr.expr);
    } catch (SQLException e) {
      if (e.getMessage().indexOf(expr.getExpectedError()) == -1) {
        fail("Expected error: " + expr.getExpectedError() + ", got: " + e.getMessage());
      }
    }
  }

  private void CheckFilters(String prefix, Pattern filter_type, Expression expr)
    throws SQLException {
    Statement statement = connection.createStatement();
    ResultSet rs = statement.executeQuery(String.format("%s %s", prefix, expr.expr));
    List<String> result = new ArrayList<String>();
    while (rs.next()) {
      String explainLine = rs.getString(1);
      if (filter_type.matcher(explainLine).find() &&
          (explainLine.contains(expr.expr) || expr.match_regex.matcher(explainLine).find()))
        return;
      result.add(explainLine);
    }
    fail(
      "Expected " + filter_type + " to be found in explain plan: " + expr + ", " + result.toString()
    );
  }

  private void CheckExprs(String prefix, List<Expression> exprs, String compatibility)
      throws Exception {
    SetCompatibilityMode(compatibility);
    for (Expression expr : exprs) {
      if (expr.behaviour == Behaviour.kNotPushable) {
        CheckFilters(prefix, kLocalFilter, expr);
      } else if (expr.behaviour == Behaviour.kPushable) {
        if (compatibility == "11")
          CheckFilters(prefix, kLocalFilter, expr);
        else
          CheckFilters(prefix, kStorageFilter, expr);
      } else if (expr.behaviour == Behaviour.kMMPushable) {
        CheckFilters(prefix, kStorageFilter, expr);
      } else {
        CheckFailure(prefix, expr);
      }
    }

    if (compatibility == "11") {
      /* Retry any SAOP expressions with SAOP pushdown disabled */
      SetMixedModeSaopPushdown("false");
      for (Expression expr : exprs) {
        if (expr.isSaopExpression()) {
          CheckFilters(prefix, kLocalFilter, expr);
        }
      }
      SetMixedModeSaopPushdown("true");
    }
  }

  Boolean isInt(String type) {
    return type == kInt2Column || type == kInt4Column || type == kInt8Column;
  }

  Boolean isFloat(String type) {
    return type == kFloat4Column || type == kFloat8Column;
  }

  String getCast(String column_name) {
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
    } else if (column_name == kTimestampCol) {
      return "timestamp without time zone";
    } else if (column_name == kTimestamptzCol) {
      return "timestamp with time zone";
    } else if (column_name == kDateCol) {
      return "date";
    } else if (column_name == kTimeCol) {
      return "time without time zone";
    } else if (column_name == kTimetzCol) {
      return "time with time zone";
    } else if (column_name == kIntervalCol) {
      return "interval";
    }
    return "";
  }

  String getModName(String column_name) {
    if (column_name == kInt2Column) {
      return "int2mod";
    } else if (column_name == kInt4Column) {
      return "int4mod";
    } else if (column_name == kInt8Column) {
      return "int8mod";
    } else if (column_name == kNumericColumn) {
      return "numeric_mod";
    }
    return "invalid mod function";
  };

  List<String> numeric_types = Arrays.asList(
    kInt2Column,
    kInt4Column,
    kInt8Column,
    kFloat4Column,
    kFloat8Column,
    kNumericColumn
  );

  List<String> comparison_operations = Arrays.asList("=", "<>", "<", "<=", ">", ">=");

  List<Expression> CreateExpressions() {
    List<Expression> exprs = new ArrayList<Expression>();

    // Add basic numeric comparisons
    for (String t1 : numeric_types) {
      for (String t2 : numeric_types) {
        for (String op : comparison_operations) {
          if (t1 == t2 && op == "=") {
            exprs.add(new Expression(String.format("(%s IS NOT NULL)", t1), Behaviour.kMMPushable));
          } else if (t1 == t2) {
            exprs.add(new Expression(String.format("(%s %s %s)", t1, op, t2),
                      Behaviour.kMMPushable));
          } else {
            // explicitly cast t2 to t1's type
            exprs.add(new Expression(String.format("(%s %s (%s)::%s)", t1, op, t2, getCast(t1)),
                      Behaviour.kMMPushable));

            // Float{4,8} can be compared directly with Float{4,8}
            // Int{2,4,8} can be compared directly with Int{2,4,8}
            if ((isFloat(t1) && isFloat(t2)) || (isInt(t1) && isInt(t2))) {
              exprs.add(new Expression(String.format("(%s %s %s)", t1, op, t2),
                        Behaviour.kMMPushable));
            }
          }
        }
      }
    }

    // Add MOD expressions
    for (String t1 : numeric_types) {
      if (isFloat(t1))
        continue;
      if (t1 == kInt4Column) {
        for (String mod_name : Arrays.asList(getModName(t1), "mod")) {
          exprs.add(new Expression(String.format("(%s(%s, 10) = 0)", mod_name, t1),
                                   Behaviour.kMMPushable));
        }
        exprs.add(new Expression(String.format("((%s %% 10) = 0)", t1),
                                 Behaviour.kMMPushable));
      } else if (isInt(t1)) {
        for (String mod_name : Arrays.asList(getModName(t1), "mod")) {
          exprs.add(new Expression(
              String.format("(%s(%s, '10'::%s) = 0)", mod_name, t1, getCast(t1)),
              Behaviour.kMMPushable));
        }
        exprs.add(new Expression(String.format("((%s %% '10'::%s) = 0)", t1, getCast(t1)),
                                 Behaviour.kMMPushable));
      } else {
        for (String mod_name : Arrays.asList("numeric_mod", "mod")) {
          exprs.add(new Expression(String.format("(%s(%s, '10'::numeric) = 0.0)", mod_name, t1),
                                   Behaviour.kMMPushable));
        }
        exprs.add(new Expression(String.format("((%s %% '10'::numeric) = 0.0)", t1),
                                 Behaviour.kMMPushable));
      }
    }

    exprs.add(new Expression(String.format("((%s)::integer = %s)", kBoolColumn, kInt4Column),
                             Behaviour.kMMPushable));
    exprs.add(new Expression(String.format("(%s = (%s)::boolean)", kBoolColumn, kInt4Column),
                             Behaviour.kMMPushable));

    // (char_col)::integer is not pushable
    exprs.add(new Expression(String.format("((%s)::integer = %s)", kCharColumn, kInt4Column),
                             Behaviour.kNotPushable));
    exprs.add(new Expression(String.format("(%s = (%s)::character(1))", kCharColumn, kInt4Column),
                             Behaviour.kNotPushable));

    // Add basic arithmetic operations (t1 arith_op t2) = t3
    List<String> arithmetic_ops = Arrays.asList("+", "*", "-", "/");
    for (String t1 : numeric_types) {
      for (String t2 : numeric_types) {
        for (String op : arithmetic_ops) {
          // arithmetic has the same type as t1
          String arithmetic_expr = (isInt(t1) == isInt(t2) && isFloat(t1) == isFloat(t2))
            ? String.format("(%s %s %s)", t1, op, t2)
            : String.format("(%s %s (%s)::%s)", t1, op, t2, getCast(t1));

          for (String t3 : numeric_types) {
            if (t3 == t1) {
              exprs.add(
                new Expression(String.format("(%s = %s)", arithmetic_expr, t3),
                Behaviour.kMMPushable));
            } else {
              // explicitly cast t2 to t1's type
              exprs.add(
                new Expression(String.format("(%s = (%s)::%s)", arithmetic_expr, t3, getCast(t1)),
                Behaviour.kMMPushable));
            }
          }
        }
      }
    }

    List<String> text_cols = Arrays.asList(kCharColumn, kTextColumn);

    // Add basic text operations
    for (String op : comparison_operations) {
      for (String t1 : text_cols) {
        for (String t2 : text_cols) {
          if (t1 == t2) {
            if (op == "=") {
              exprs.add(
                new Expression(String.format("(%s IS NOT NULL)", t1, op, t2), Behaviour.kMMPushable)
              );
            } else {
              exprs.add(
                new Expression(String.format("(%s %s %s)", t1, op, t2), Behaviour.kMMPushable)
              );
            }
          } else if (t1 == kTextColumn) { // t2 is char
            exprs.add(
              new Expression(String.format("(%s %s (%s)::text)", t1, op, t2), Behaviour.kMMPushable)
            );
            exprs.add(
              new Expression(
                String.format("((%s)::character(1) %s %s)", t1, op, t2),
                Behaviour.kMMPushable
              )
            );
          } else { // t1 is char, t2 is text
            exprs.add(
              new Expression(String.format("((%s)::text %s %s)", t1, op, t2), Behaviour.kMMPushable)
            );
            exprs.add(
              new Expression(
                String.format("(%s %s (%s)::character(1))", t1, op, t2),
                Behaviour.kMMPushable
              )
            );
          }
        }
      }
    }

    // Add boolean operators
    List<String> boolean_ops = Arrays.asList("AND", "OR");
    for (String op : boolean_ops) {
      exprs.add(
        new Expression(
          String.format("(%s %s (%s = 3))", kBoolColumn, op, kInt4Column),
          Behaviour.kMMPushable
        )
      );
    }

    for (String op : comparison_operations) {
      exprs.add(
        new Expression(
          String.format("(%s %s (NOT %s))", kBoolColumn, op, kBoolColumn),
          Behaviour.kMMPushable
        )
      );
    }

    // Add unary types
    for (String t1 : numeric_types) {
      String t2 = isFloat(t1)
        ? "'-1'::double precision"
        : isInt(t1) ? "'-1'::integer" : "'-1'::numeric";
      exprs.add(new Expression(String.format("((- %s) = %s)", t1, t2), Behaviour.kMMPushable));
      exprs.add(new Expression(String.format("(abs(%s) = %s)", t1, t2), Behaviour.kMMPushable));

      String col_type = t1.substring(0, t1.indexOf("_"));
      String opt_underscore = (t1 == kNumericColumn) ? "_" : "";
      exprs.add(
        new Expression(
          String.format("(%s%sabs(%s) = %s)", col_type, opt_underscore, t1, t2),
          Behaviour.kMMPushable
        )
      );
    }

    exprs.add(new Expression(String.format("(NOT %s)", kBoolColumn), Behaviour.kMMPushable));

    // Add UUID comparisons
    for (String op : comparison_operations) {
      exprs.add(
        new Expression(
          String.format("(%s %s '12345679-1234-5678-1234-123456789012'::uuid)", kUuidColumn, op),
          Behaviour.kMMPushable
        )
      );
    }

    return exprs;
  }

  @Test
  public void TestScalarArrayOpExprs() throws Exception {
    List<Expression> exprs = Arrays.asList(
      String.format("(%s = ANY ('{1,2}'::integer[]))", kInt4Column),
      String.format("(%s <> ALL ('{1,2}'::text[]))", kTextColumn),
      String.format("((%s = ANY ('{1,2}'::integer[])) AND (%s = ANY ('{1,2}'::text[])))",
                        kInt4Column, kTextColumn),
      String.format("((%s = ANY ('{1,2}'::integer[])) OR (%s = ANY ('{1,2}'::text[])))",
                        kInt4Column, kTextColumn),
      String.format("(%s ~~ ANY ('{1%%,2%%}'::text[]))", kTextColumn)
    ).stream().map(e -> new Expression(e, Behaviour.kMMPushable)).collect(Collectors.toList());

    TestPushdowns(
      String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTableName),
      exprs
    );
  }

  @Test
  public void TestTableFilters() throws Exception {
    List<Expression> exprs = CreateExpressions();

    exprs.add(
      new Expression(String.format("(%s = '1'::text)", kTextColumn), Behaviour.kMMPushable)
    );

    for (String expr : Arrays.asList(
      String.format("((%s <-> '(1,1)'::point) < '1'::double precision)", kPointColumn),
      String.format("CASE WHEN (%s = 1) THEN true ELSE false END", kInt4Column),
      String.format("(%s = '1'::name)", kNameColumn)
    )) {
      exprs.add(new Expression(expr, Behaviour.kPushable));
    }

    TestPushdowns(
      String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTableName),
      exprs
    );
  }

  @Test
  public void TestOutOfBoundsConversions() throws Exception {
    {
      Statement statement = connection.createStatement();
      for (int i = 10; i < 40; i++) {
        InsertPowerOfTwoRow(statement, i);
      }
    }

    List<Expression> exprs = new ArrayList<Expression>();
    List<String> int_cols = Arrays.asList(kInt2Column, kInt4Column, kInt8Column);
    for (String t1 : int_cols) {
      for (String t2 : int_cols) {
        if (
          t1 == t2
        ) continue; // comparing the same col results in IS NOT NULL

        // no explicit casting is fine because they are implicitly upcasted
        exprs.add(new Expression(String.format("(%s = %s)", t1, t2), Behaviour.kMMPushable));

        String cond = String.format("(%s = (%s)::%s)", t1, t2, getCast(t1));
        if (t1.compareTo(t2) < 0) {
          // casting t2 to a smaller type will cause an error
          exprs.add(new Expression(cond, Behaviour.kOutofRangeError));
        } else {
          // casting t2 to a larger type is fine
          exprs.add(new Expression(cond, Behaviour.kMMPushable));
        }
      }
    }

    TestPushdowns(
      String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTableName),
      exprs
    );
  }

  @Test
  public void TestIndexFilters() throws Exception {
    String kIndexName = "idx";

    Statement statement = connection.createStatement();
    statement.execute(
      String.format(
        "CREATE INDEX %s ON %s (%s ASC) INCLUDE (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)",
        kIndexName,
        kTableName,
        kNameColumn,
        // Included Cols
        kInt2Column,
        kInt4Column,
        kInt8Column,
        kFloat4Column,
        kFloat8Column,
        kTextColumn,
        kPointColumn,
        kBoxColumn,
        kTidColumn,
        kBoolColumn
      )
    );

    String prefix = String.format(
      "/* IndexScan(%s) */ EXPLAIN %s SELECT * FROM %s WHERE %s < '9' AND ",
      kIndexName,
      kExplainArgs,
      kTableName,
      kNameColumn
    );

    List<Expression> exprs = CreateExpressions();

    for (String condition : Arrays.asList(
      String.format("((%s <-> '(1,1)'::point) < '1'::double precision)", kPointColumn),
      String.format("CASE WHEN (%s = 1) THEN true ELSE false END", kInt4Column)
    )) {
      exprs.add(new Expression(condition, Behaviour.kPushable));
    }

    TestPushdowns(prefix, exprs);
  }

  @Test
  public void TestSystemTables() throws Exception {
    String prefix = String.format(
      "/*+ Set(enable_indexscan off) */ EXPLAIN %s SELECT * FROM pg_class WHERE",
      kExplainArgs
    );

    TestPushdowns(
      prefix,
      Arrays.asList(
        new Expression("(relname = 'pg_proc'::name)", Behaviour.kPushable),
        new Expression("(relowner = '10'::oid)", Behaviour.kPushable),
        new Expression("(relkind = 'r'::\"char\")", Behaviour.kPushable)
      )
    );
  }

  @Test
  public void TestNewPg15Functions() throws Exception {
    TestPushdowns(
      String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTableName),
      Arrays.asList(
        new Expression(
          String.format("(('[(1,1),(1,1)]'::lseg <-> %s) < '1'::double precision)", kBoxColumn),
          Behaviour.kPushable), // lseg <-> box existed in PG11
        new Expression(
          String.format("((%s <-> '[(1,1),(1,1)]'::lseg) < '1'::double precision)", kBoxColumn),
          Behaviour.kPushable), // box <-> lseg was added in PG15
        new Expression(String.format("(hashtid(%s) = 1)", kTidColumn), Behaviour.kPushable),
        new Expression(String.format("(log10((%s)::double precision) < '10'::double precision)",
                                     kInt4Column),
                       Behaviour.kPushable),
        new Expression(String.format("(%s |>> '(1,1)'::point)", kPointColumn),
                       Behaviour.kPushable),
        new Expression(String.format("(%s <<| '(1,1)'::point)", kPointColumn),
                       Behaviour.kPushable),
        new Expression(
          String.format("(('((1,0),(1,1),(1,1))'::polygon <-> %s) < '10'::double precision)",
                        kCircleColumn),
          Behaviour.kPushable)
      )
    );
  }

  Boolean hasDate(String type) {
    return type == kDateCol || type == kTimestampCol || type == kTimestamptzCol;
  }

  Boolean hasTimezone(String type) {
    return type == kTimestamptzCol || type == kTimetzCol;
  }

  @Test
  public void TestStringOperations() throws Exception {
    final String kTable = "text_tbl";
    final String kTextColumn = "text_col";
    final String kVarcharColumn = "varchar_col";
    final String kCharColumn = "char_col";
    final String kBpCharColumn = "bpchar_col";

    {
      Statement statement = connection.createStatement();
      String createTable = String.format(
        "CREATE TABLE %s(%s TEXT, %s VARCHAR, %s CHAR, %s BPCHAR)",
        kTable, kTextColumn, kVarcharColumn, kCharColumn, kBpCharColumn
      );
      LOG.info("Creating table: " + createTable);
      statement.execute(createTable);

      statement.execute(
          String.format("INSERT INTO %s VALUES ('hello', 'world', 'a', 'b')", kTable));
    }

    List<Expression> exprs = new ArrayList<Expression>();
    for (String cond : Arrays.asList(
      String.format("(%s ~~ 'h%%'::text)", kTextColumn), // F_TEXTLIKE
      String.format("(%s !~~ 'h%%'::text)", kTextColumn), // F_TEXTNLIKE
      String.format("(%s ~~ (%s)::text)", kTextColumn, kVarcharColumn), // F_LIKE_TEXT_TEXT
      String.format("(%s !~~ (%s)::text)", kTextColumn, kVarcharColumn), // F_NOTLIKE_TEXT_TEXT
      String.format("(%s ~~ 'b%%'::text)", kBpCharColumn), // F_BPCHARLIKE
      String.format("(%s !~~ 'b%%'::text)", kBpCharColumn), // F_BPCHARNLIKE
      String.format("(%s ~~* 'H%%'::text)", kTextColumn), // F_TEXTICLIKE
      String.format("(%s !~~* 'H%%'::text)", kTextColumn), // F_TEXTICNLIKE
      String.format("(%s ~~* 'B%%'::text)", kBpCharColumn), // F_BPCHARICLIKE
      String.format("(%s !~~* 'B%%'::text)", kBpCharColumn), // F_BPCHARICNLIKE
      String.format("(%s ~ 'h.*'::text)", kTextColumn), // F_TEXTREGEXEQ
      String.format("(%s !~ 'h.*'::text)", kTextColumn), // F_TEXTREGEXNE
      String.format("(ascii(%s) = 104)", kTextColumn), // F_ASCII

      // F_SUBSTRING_TEXT_TEXT
      String.format("(\"substring\"(%s, 'h.'::text) = 'he'::text)", kTextColumn),

      // F_SUBSTRING_TEXT_INT4
      String.format("(\"substring\"(%s, 2) = 'ello'::text)", kTextColumn),

      // F_SUBSTRING_TEXT_INT4_INT4
      String.format("(\"substring\"(%s, 2, 3) = 'ell'::text)", kTextColumn)
    )) {
      exprs.add(new Expression(cond, Behaviour.kMMPushable));
    }

    // special cases: the formatting for these conditions changes in the output depending on the
    // YSQL version, so use a regex to check that the correct condition is present in the output
    // F_LIKE_TEXT_TEXT
    exprs.add(new Expression(String.format("like(%s, 'h%%'::text)", kTextColumn),
                             Behaviour.kMMPushable, Pattern.compile(".*like.*")));
    // F_NOTLIKE_TEXT_TEXT
    exprs.add(new Expression(String.format("notlike(%s, 'h%%'::text)", kTextColumn),
                             Behaviour.kMMPushable, Pattern.compile(".*notlike.*")));

    // these functions don't exist in PG11, so they can't be pushed
    exprs.add(new Expression(String.format("regexp_like(%s, 'h.*'::text)", kTextColumn),
                             Behaviour.kPushable, Pattern.compile(".*regexp_like.*")));

    for (String cond : Arrays.asList(
      String.format("(regexp_substr(%s, 'h.'::text) = 'he'::text)", kTextColumn),
      String.format("(regexp_substr(%s, 'h.'::text, 1) = 'he'::text)", kTextColumn)
    )) {
      exprs.add(new Expression(cond, Behaviour.kPushable));
    }

    String prefix = String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTable);
    TestPushdowns(prefix, exprs);
  }

  @Test
  public void TestTimePushdowns() throws Exception {
    final String kTimeTable = "time_tbl";

    {
      Statement statement = connection.createStatement();
      String createTable = String.format(
        "CREATE TABLE %s(%s timestamp, %s timestamptz, %s date, %s time, %s timetz, %s interval)",
        kTimeTable, kTimestampCol, kTimestamptzCol, kDateCol, kTimeCol, kTimetzCol, kIntervalCol
      );
      LOG.info("Creating table: " + createTable);
      statement.execute(createTable);

      for (int i = 1; i <= 12; i++) {
        statement.execute(String.format(
            "INSERT INTO %s VALUES ('2022-%s-%s 00:00:00', '2022-%s-%s 00:00:00+00', " +
            "'2022-%s-%s', '%s:00:00', '%s:00:00+00', '%s days')",
            kTimeTable, i, i, i, i, i, i, i, i, i, i));
      }
    }

    List<String> time_cols = Arrays.asList(
      kTimestampCol, kTimestamptzCol, kDateCol, kTimeCol, kTimetzCol
    );

    List<Expression> exprs = new ArrayList<Expression>();
    for (String op : comparison_operations) {
      exprs.add(new Expression(String.format("(%s %s '1 day'::interval)", kIntervalCol, op),
                            Behaviour.kMMPushable));
      for (String col : time_cols) {
        exprs.add(new Expression(
            String.format("(%s %s '2022-01-01 00:00:00'::%s)", col, op, getCast(col)),
            Behaviour.kMMPushable,
            // The timezone is modified to use the server timezone, so omit the actual time value.
            Pattern.compile(String.format("%s %s .*::%s", col, op, getCast(col)))));
      }
    }

    // Try casting between all time types. Most must be done locally, but there are some exceptions.
    for (String from : time_cols) {
      for (String to : time_cols) {
        if (from == to)
          continue;

        Behaviour behaviour = Behaviour.kNotPushable;
        if (!hasTimezone(to) && from == kTimestampCol)
          behaviour = Behaviour.kPushable;
        else if (!hasDate(to) && from == kDateCol)
          behaviour = Behaviour.kBadCastError;
        else if (hasDate(to) && !hasDate(from))
          behaviour = Behaviour.kBadCastError;
        else if (to == kTimeCol && from == kTimetzCol)
          behaviour = Behaviour.kPushable;
        else if (to == kTimestampCol && from == kDateCol)
          behaviour = Behaviour.kPushable;
        else if (to == kTimetzCol && from == kTimestampCol)
          behaviour = Behaviour.kBadCastError;

        exprs.add(new Expression(String.format("(%s = (%s)::%s)", to, from, getCast(to)),
                                  behaviour));
      }
    }

    TestPushdowns(String.format("EXPLAIN %s SELECT * FROM %s WHERE", kExplainArgs, kTimeTable),
                          exprs);
  }
}
