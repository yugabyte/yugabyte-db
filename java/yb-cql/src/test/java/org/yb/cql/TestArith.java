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
package org.yb.cql;

import java.util.*;

import org.junit.Test;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestArith extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestArith.class);

  interface MathOperator {
    Long Eval(Long a, Long b);
  }

  public class IntPlus implements MathOperator {
    @Override
    public Long Eval(Long a, Long b) {
      if (a == null || b == null) return null;
      return (a + b);
    }
  }

  public class IntMinus implements MathOperator {
    @Override
    public Long Eval(Long a, Long b) {
      if (a == null || b == null) return null;
      return (a - b);
    }
  }

  public class CounterPlus implements MathOperator {
    @Override
    public Long Eval(Long a, Long b) {
      if (a == null) a = 0L;
      return (a + b);
    }
  }

  public class CounterMinus implements MathOperator {
    @Override
    public Long Eval(Long a, Long b) {
      if (a == null) a = 0L;
      return (a - b);
    }
  }

  public void EvalCountingOp(String yqltype, String yqlop, MathOperator op) throws Exception {
    LOG.info(String.format("TEST CQL %s ARITHMETIC - Start", yqltype));

    // Setup test table.
    session.execute(String.format("DROP TABLE IF EXISTS test_%s", yqltype));
    String create_stmt =
        String.format("CREATE TABLE test_%s (h1 int primary key, c1 %s, c2 %s, c3 %s);",
                      yqltype, yqltype, yqltype, yqltype);
    session.execute(create_stmt);

    // To create the same flow for all types, insert "seed" into non-counter table.
    if (yqltype != "COUNTER") {
      String insert_stmt =
        String.format("INSERT INTO test_%s(h1) VALUES(1);", yqltype);
      session.execute(insert_stmt);
    }

    // Update: c1 = c1 + seed where c1 is null.
    Long test_seed = 77L;
    String update_stmt = String.format("UPDATE test_%s SET c1 = c1 %s %d WHERE h1 = 1;",
                                       yqltype, yqlop, test_seed);
    session.execute(update_stmt);

    // Select data from the test table.
    String select_stmt =
      String.format("SELECT h1, c1, c2, c3 FROM test_%s WHERE h1 = 1;", yqltype);
    ResultSet rs = session.execute(select_stmt);

    Long c1_value = 0L;
    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));

      c1_value = op.Eval(null, test_seed);
      if (c1_value == null) {
        assertTrue(row.isNull(1));
      } else {
        assertEquals(c1_value.longValue(), row.getLong(1));
      }

      assertTrue(row.isNull(3));
      row_count++;
    }
    assertEquals(1, row_count);

    // To create the same flow for all types, update c1 with "seed" for non-counter table.
    if (yqltype != "COUNTER") {
      String insert_stmt =
        String.format("UPDATE test_%s SET c1 = 0 %s %d WHERE h1 = 1;", yqltype, yqlop, test_seed);
      c1_value = op.Eval(0L, test_seed);
      session.execute(insert_stmt);
    }

    // Update: c1 = c1 + seed where c1 == seed.
    update_stmt =
      String.format("UPDATE test_%s SET c1 = c1 %s %d WHERE h1 = 1;", yqltype, yqlop, test_seed);
    session.execute(update_stmt);

    // Select data from the test table and check.
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c1_value = op.Eval(c1_value, test_seed);
      assertEquals(c1_value.longValue(), row.getLong(1));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update: c1 = c1 + -seed where c1 == 2 * seed (Test counting op with negative values).
    update_stmt =
        String.format("UPDATE test_%s SET c1 = c1 %s %d WHERE h1 = 1;", yqltype, yqlop, -test_seed);
    session.execute(update_stmt);

    // Select data from the test table and check.
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c1_value = op.Eval(c1_value, -test_seed);
      assertEquals(c1_value.longValue(), row.getLong(1));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update with out-of-range values: positive value (Check overflow).
    update_stmt = String.format("UPDATE test_%s SET c1 = c1 %s 9223372036854775808 WHERE h1 = 1;",
        yqltype, yqlop);
    runInvalidStmt(update_stmt);

    // Update with out-of-range values: negative value (Check overflow).
    update_stmt = String.format("UPDATE test_%s SET c1 = c1 %s -9223372036854775809 WHERE h1 = 1;",
        yqltype, yqlop);
    runInvalidStmt(update_stmt);
  }

  public void EvalBigIntOp(String yqlop, MathOperator op) throws Exception {
    LOG.info("TEST CQL BIGINT ARITHMETIC - Start");

    // Setup test table.
    session.execute("DROP TABLE IF EXISTS test_BIGINT");
    String create_stmt =
      "CREATE TABLE test_BIGINT(h1 INT PRIMARY KEY, c1 BIGINT, c2 BIGINT, c3 BIGINT);";
    session.execute(create_stmt);

    // Update: c2 = c2 <operator> <a value>, where c2 is null. Result should be null.
    Long test_seed = 77L;
    Long c1_value = 0L;
    String update_stmt =
        String.format("UPDATE test_BIGINT SET c1 = %d, c2 = c2 %s %d WHERE h1 = 1;",
                      test_seed, yqlop, test_seed);
    session.execute(update_stmt);

    // Select data from the test table.
    String select_stmt = "SELECT h1, c1, c2, c3 FROM test_BIGINT where h1 = 1;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c1_value = test_seed;
      assertEquals(c1_value.longValue(), row.getLong(1));
      assertTrue(row.isNull(2));
      assertTrue(row.isNull(3));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update:
    //   c1 = c1 <operator> <a value>
    //   c2 = c1 <operator> c1
    // Where c1 is NOT null. The result should be NOT null.
    Long test_seed2 = 20L;
    update_stmt = String.format(
        "UPDATE test_BIGINT SET c1 = c1 %s %d, c2 = c1 %s c1, c3 = c3 %s 1 WHERE h1 = 1;",
        yqlop, test_seed2, yqlop, yqlop);
    session.execute(update_stmt);

    // Select data from the test table.
    Long c2_value = 0L;
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));

      c2_value = op.Eval(c1_value, c1_value);
      c1_value = op.Eval(c1_value, test_seed2);
      assertEquals(c1_value.longValue(), row.getLong(1));
      assertEquals(c2_value.longValue(), row.getLong(2));

      assertTrue(row.isNull(3));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update:
    //   c2 = c1 + c2 IF c1 = <a value>
    // Evaluate both expression and IF clause.
    update_stmt = String.format(
        "UPDATE test_BIGINT SET c2 = c1 %s c2 WHERE h1 = 1 IF c1 = %d;", yqlop, c1_value);
    session.execute(update_stmt);

    // Select data from the test table.
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c2_value = op.Eval(c1_value, c2_value);
      assertEquals(c2_value.longValue(), row.getLong(2));
      row_count++;
    }
    assertEquals(1, row_count);
  }

  public void EvalIntOp(String yqlop, MathOperator op) throws Exception {
    LOG.info("TEST CQL INT ARITHMETIC - Start");

    // Setup test table.
    session.execute("DROP TABLE IF EXISTS test_INT");
    String create_stmt =
      "CREATE TABLE test_INT(h1 INT PRIMARY KEY, c1 INT, c2 INT, c3 SMALLINT, c4 TINYINT);";
    session.execute(create_stmt);

    // Update: c2 = c2 <operator> <a value>, where c2 is null. Result should be null.
    Long test_seed = 77L;
    Long c1_value = 0L;
    String update_stmt =
        String.format("UPDATE test_INT SET c1 = %d, c2 = c2 %s %d WHERE h1 = 1;",
                      test_seed, yqlop, test_seed);
    session.execute(update_stmt);

    // Select data from the test table.
    String select_stmt = "SELECT h1, c1, c2, c3, c4 FROM test_INT where h1 = 1;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c1_value = test_seed;
      assertEquals(c1_value.longValue(), row.getInt(1));
      assertTrue(row.isNull(2));
      assertTrue(row.isNull(3));
      assertTrue(row.isNull(4));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update:
    //   c1 = c1 <operator> <a value>
    //   c2 = c1 <operator> c1
    // Where c1 is NOT null. The result should be NOT null.
    Long test_seed2 = 20L;
    update_stmt = String.format(
      "UPDATE test_INT SET c1 = c1 %s %d, c2 = c1 %s c1, c3 = c1 %s 3, c4 = c1 %s 4 WHERE h1 = 1;",
      yqlop, test_seed2, yqlop, yqlop, yqlop);
    session.execute(update_stmt);

    // Select data from the test table.
    Long c2_value = 0L;
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));

      c2_value = op.Eval(c1_value, c1_value);
      Long c3_value = op.Eval(c1_value, 3L);
      Long c4_value = op.Eval(c1_value, 4L);
      c1_value = op.Eval(c1_value, test_seed2);
      assertEquals(c1_value.longValue(), row.getInt(1));
      assertEquals(c2_value.longValue(), row.getInt(2));
      assertEquals(c3_value.longValue(), row.getShort(3));
      assertEquals(c4_value.longValue(), row.getByte(4));
      row_count++;
    }
    assertEquals(1, row_count);

    // Update:
    //   c2 = c1 + c2 IF c1 = <a value>
    // Evaluate both expression and IF clause.
    update_stmt = String.format(
        "UPDATE test_INT SET c1 = c1, c2 = c1 %s c2 WHERE h1 = 1 IF c1 = %d;", yqlop, c1_value);
    session.execute(update_stmt);

    // Select data from the test table.
    rs = session.execute(select_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      assertEquals(1, row.getInt(0));
      c2_value = op.Eval(c1_value, c2_value);
      assertEquals(c2_value.longValue(), row.getInt(2));
      row_count++;
    }
    assertEquals(1, row_count);
  }

  @Test
  public void testCountingOp() throws Exception {
    EvalCountingOp("BIGINT", "+", new IntPlus());
    EvalCountingOp("BIGINT", "-", new IntMinus());

    EvalCountingOp("COUNTER", "+", new CounterPlus());
    EvalCountingOp("COUNTER", "-", new CounterMinus());
  }

  @Test
  public void testNumericOp() throws Exception {
    EvalBigIntOp("+", new IntPlus());
    EvalBigIntOp("-", new IntMinus());

    EvalIntOp("+", new IntPlus());
    EvalIntOp("-", new IntMinus());
  }

  @Test
  public void testCounterWithBind() throws Exception {
    // Setting up
    session.execute("CREATE TABLE test_arith_bind_counter(h INT PRIMARY KEY, c COUNTER);");

    // Testing plus.
    session.execute("UPDATE test_arith_bind_counter SET c = c + ? where h = ?",
            new Long(3), new Integer(1));
    Row row = runSelect("SELECT * from test_arith_bind_counter where h = 1").next();
    assertEquals(3, row.getLong("c"));

    // Testing minus.
    session.execute("UPDATE test_arith_bind_counter SET c = c - ? where h = ?",
            new Long(2), new Integer(1));

    row = runSelect("SELECT * from test_arith_bind_counter where h = 1").next();
    assertEquals(1, row.getLong("c"));
  }

  @Test
  public void testCounterError() throws Exception {
    LOG.info("TEST CQL ERRONEOUS COUNTER ARITHMETIC - Start");

    // Setup test table.
    String create_stmt =
      "CREATE TABLE test_counter (h1 int primary key, c1 counter, c2 counter, c3 counter);";
    session.execute(create_stmt);

    // Insert is not allowed for COUNTER.
    runInvalidStmt("INSERT INTO test_counter(h1, c1) VALUES(1, 2);");

    // Update with constant.
    runInvalidStmt("UPDATE test_counter SET c1 = 2 WHERE h1 = 1;");

    // Update with wrong column.
    runInvalidStmt("UPDATE test_counter SET c1 = c2 + 3 WHERE h1 = 2;");

    // Update with IF clause.
    runInvalidStmt("UPDATE test_counter SET c1 = c1 + 1 WHERE h1 = 1 IF c3 = 0;");
  }
}
