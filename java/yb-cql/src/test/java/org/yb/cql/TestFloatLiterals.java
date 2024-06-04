// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.*;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestFloatLiterals extends BaseCQLTest {

  float float_infinity_positive = createFloat(0, 0b11111111, 0);
  float float_infinity_negative = createFloat(1, 0b11111111, 0);
  float float_nan_0 = createFloat(0, 0b11111111, 1);
  float float_nan_1 = createFloat(1, 0b11111111, 1);
  float float_nan_2 = createFloat(0, 0b11111111, 0b10);
  float float_zero_positive = createFloat(0, 0, 0);
  float float_zero_negative = createFloat(1, 0, 0);
  float float_sub_normal = createFloat(0, 0, 1);

  double double_infinity_positive = createDouble(0, 0b11111111111, 0);
  double double_infinity_negative = createDouble(1, 0b11111111111, 0);
  double double_nan_0 = createDouble(0, 0b011111111111, 1);
  double double_nan_1 = createDouble(0, 0b111111111111, 1);
  double double_nan_2 = createDouble(0, 0b011111111111, 0b10);
  double double_zero_positive = createDouble(0, 0, 0);
  double double_zero_negative = createDouble(1, 0, 0);
  double double_sub_normal = createDouble(0, 0, 1);

  float[] float_all_literals = { float_infinity_positive, float_infinity_negative, float_nan_0,
      float_nan_1, float_nan_2, float_zero_positive, float_zero_negative, float_sub_normal };
  double[] double_all_literals = { double_infinity_positive, double_infinity_negative, double_nan_0,
      double_nan_1, double_nan_2, double_zero_positive, double_zero_negative, double_sub_normal };

  private float createFloat(int sign, int exp, int fraction) {
    return Float.intBitsToFloat((sign << 31) | (exp << 23) | fraction);
  }

  private double createDouble(long sign, long exp, long fraction) {
    return Double.longBitsToDouble((sign << 63) | (exp << 52) | fraction);
  }

  @Test
  public void testPartitionKeyLiteralsFloat() throws Exception {
    session.execute("create table t (h float, v int, primary key (h, v));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, v) values (?, ?)";
    for (int i = 0; i < float_all_literals.length; i++) {
      session.execute(insert_stmt, float_all_literals[i], i);
    }

    // Insert nan and infinity directly.
    session.execute("insert into t (h, v) values (Infinity, 8);");
    session.execute("insert into t (h, v) values (-Infinity, 9);");
    session.execute("insert into t (h, v) values (NaN, 10);");

    session.execute(insert_stmt, Float.NaN, 11);
    session.execute(insert_stmt, Float.POSITIVE_INFINITY, 12);
    session.execute(insert_stmt, Float.NEGATIVE_INFINITY, 13);
    session.execute(insert_stmt, Float.MAX_VALUE, 14);
    session.execute(insert_stmt, Float.MIN_VALUE, 15);

    String select_stmt = "select v from t where h = ?;";

    // First, ensure that positive and negative infinity are different

    ResultSet rs = session.execute("select v from t where h = Infinity;");
    assertEquals(3, rs.all().size());

    rs = session.execute("select v from t where h = -Infinity;");
    assertEquals(3, rs.all().size());

    // Now ensure that a query on nan returns 4 results
    rs = session.execute("select v from t where h = NaN;");
    assertEquals(5, rs.all().size());

    // Ensure that a query on 0 returns just 1 result
    rs = session.execute("select v from t where h = 0;");
    assertEquals(1, rs.all().size());

    // Do a few bind variable tests
    rs = session.execute(select_stmt, float_nan_0);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, float_nan_1);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, float_nan_2);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, float_zero_negative);
    assertEquals(1, rs.all().size());
    rs = session.execute(select_stmt, float_sub_normal);
    assertEquals(2, rs.all().size());
    rs = session.execute(select_stmt, Float.MAX_VALUE);
    assertEquals(1, rs.all().size());
    rs = session.execute(select_stmt, Float.MIN_VALUE);
    assertEquals(2, rs.all().size());
  }

  @Test
  public void testClusteringKeyLiteralsFloat() throws Exception {
    session.execute("create table t (h int, c float, v int, primary key (h, c));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, c, v) values (?, ?, ?)";
    for (int i = 0; i < float_all_literals.length; i++) {
      session.execute(insert_stmt, 0, float_all_literals[i], i);
    }

    session.execute("insert into t (h, c, v) values (0, Infinity, 8);");
    session.execute("insert into t (h, c, v) values (0, -Infinity, 9);");
    session.execute("insert into t (h, c, v) values (0, NaN, 10);");
    session.execute("insert into t (h, c, v) values (0, 0.0, 11);");
    session.execute("insert into t (h, c, v) values (0, -0.0, 12);");
    session.execute("insert into t (h, c, v) values (0, 1.5, 13);");

    session.execute(insert_stmt, 0, Float.NaN, 14);
    session.execute(insert_stmt, 0, Float.POSITIVE_INFINITY, 15);
    session.execute(insert_stmt, 0, Float.NEGATIVE_INFINITY, 16);
    session.execute(insert_stmt, 0, Float.MAX_VALUE, 17);
    session.execute(insert_stmt, 0, Float.MIN_VALUE, 18);

    // Now select ordering by v

    ResultSet rs = session.execute("select v from t where h = 0 order by c;");
    List<Row> rows = rs.all();
    assertEquals(8, rows.size());

    assertEquals(16, rows.get(0).getInt(0));
    assertEquals(12, rows.get(1).getInt(0));
    assertEquals(11, rows.get(2).getInt(0));
    assertEquals(18, rows.get(3).getInt(0));
    assertEquals(13, rows.get(4).getInt(0));
    assertEquals(17, rows.get(5).getInt(0));
    assertEquals(15, rows.get(6).getInt(0));
    assertEquals(14, rows.get(7).getInt(0));
  }

  @Test
  public void testValueLiteralsFloat() throws Exception {
    session.execute("create table t (h int, v1 float, v2 int, primary key (h));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, v1, v2) values (0, ?, ?)";
    session.execute(insert_stmt, Float.NaN, 0);

    String update_stmt = "update t set v2 = ? where h = 0 if v1 = ?;";
    session.execute(update_stmt, 1, float_infinity_positive);

    ResultSet rs = session.execute("select v2 from t where h = 0");
    assertEquals(0, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 1, float_nan_0);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(1, rs.all().get(0).getInt(0));

    session.execute("update t set v2 = 2 where h = 0 if v1 = NAN;");

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(2, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 3, float_nan_1);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(3, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 4, float_nan_2);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(4, rs.all().get(0).getInt(0));
  }

  @Test
  public void testPartitionKeyLiteralsDouble() throws Exception {
    session.execute("create table t (h double, v int, primary key (h, v));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, v) values (?, ?)";
    for (int i = 0; i < double_all_literals.length; i++) {
      session.execute(insert_stmt, double_all_literals[i], i);
    }

    // Insert nan and infinity directly.
    session.execute("insert into t (h, v) values (infinity, 8);");
    session.execute("insert into t (h, v) values (-Infinity, 9);");
    session.execute("insert into t (h, v) values (NaN, 10);");

    session.execute(insert_stmt, Double.NaN, 11);
    session.execute(insert_stmt, Double.POSITIVE_INFINITY, 12);
    session.execute(insert_stmt, Double.NEGATIVE_INFINITY, 13);
    session.execute(insert_stmt, Double.MAX_VALUE, 14);
    session.execute(insert_stmt, Double.MIN_VALUE, 15);

    String select_stmt = "select v from t where h = ?;";

    ResultSet rs = session.execute("select v from t where h = INFINITY;");
    assertEquals(3, rs.all().size());

    rs = session.execute("select v from t where h = -Infinity;");
    assertEquals(3, rs.all().size());

    // Now ensure that a query on nan returns 4 results
    rs = session.execute("select v from t where h = NaN;");
    assertEquals(5, rs.all().size());

    // Ensure that a query on 0 returns just 1 result
    rs = session.execute("select v from t where h = 0;");
    assertEquals(1, rs.all().size());

    // Do a few bind variable tests
    rs = session.execute(select_stmt, double_nan_0);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, double_nan_1);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, double_nan_2);
    assertEquals(5, rs.all().size());
    rs = session.execute(select_stmt, double_zero_negative);
    assertEquals(1, rs.all().size());
    rs = session.execute(select_stmt, double_sub_normal);
    assertEquals(2, rs.all().size());
    rs = session.execute(select_stmt, Double.MAX_VALUE);
    assertEquals(1, rs.all().size());
    rs = session.execute(select_stmt, Double.MIN_VALUE);
    assertEquals(2, rs.all().size());
  }

  @Test
  public void testClusteringKeyLiteralsDouble() throws Exception {
    session.execute("create table t (h int, c double, v int, primary key (h, c));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, c, v) values (?, ?, ?)";
    for (int i = 0; i < double_all_literals.length; i++) {
      session.execute(insert_stmt, 0, double_all_literals[i], i);
    }

    session.execute("insert into t (h, c, v) values (0, Infinity, 8);");
    session.execute("insert into t (h, c, v) values (0, -INFINITY, 9);");
    session.execute("insert into t (h, c, v) values (0, NaN, 10);");
    session.execute("insert into t (h, c, v) values (0, 0.0, 11);");
    session.execute("insert into t (h, c, v) values (0, -0.0, 12);");
    session.execute("insert into t (h, c, v) values (0, 1.5, 13);");

    session.execute(insert_stmt, 0, Double.NaN, 14);
    session.execute(insert_stmt, 0, Double.POSITIVE_INFINITY, 15);
    session.execute(insert_stmt, 0, Double.NEGATIVE_INFINITY, 16);
    session.execute(insert_stmt, 0, Double.MAX_VALUE, 17);
    session.execute(insert_stmt, 0, Double.MIN_VALUE, 18);

    // Now select ordering by v

    ResultSet rs = session.execute("select v from t where h = 0 order by c;");
    List<Row> rows = rs.all();
    assertEquals(8, rows.size());

    assertEquals(16, rows.get(0).getInt(0));
    assertEquals(12, rows.get(1).getInt(0));
    assertEquals(11, rows.get(2).getInt(0));
    assertEquals(18, rows.get(3).getInt(0));
    assertEquals(13, rows.get(4).getInt(0));
    assertEquals(17, rows.get(5).getInt(0));
    assertEquals(15, rows.get(6).getInt(0));
    assertEquals(14, rows.get(7).getInt(0));
  }

  @Test
  public void testValueLiteralsDouble() throws Exception {
    session.execute("create table t (h int, v1 double, v2 int, primary key (h));");

    // Insert all the literals into the table
    String insert_stmt = "insert into t (h, v1, v2) values (0, ?, ?)";
    session.execute(insert_stmt, Double.NaN, 0);

    String update_stmt = "update t set v2 = ? where h = 0 if v1 = ?;";
    session.execute(update_stmt, 1, double_infinity_positive);

    ResultSet rs = session.execute("select v2 from t where h = 0");
    assertEquals(0, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 1, double_nan_0);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(1, rs.all().get(0).getInt(0));

    session.execute("update t set v2 = 2 where h = 0 if v1 = NAN;");

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(2, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 3, double_nan_1);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(3, rs.all().get(0).getInt(0));

    session.execute(update_stmt, 4, double_nan_2);

    rs = session.execute("select v2 from t where h = 0");
    assertEquals(4, rs.all().get(0).getInt(0));
  }

  @Test
  public void testInvalidLiterals() throws Exception {
    runInvalidStmt("create table infinity (h float primary key);");
    runInvalidStmt("create table nan (h float primary key);");
    session.execute("create table t (i int primary key);");
    runInvalidStmt("insert into t (i) values (INFINITY)");
    runInvalidStmt("insert into t (i) values (NAN)");
  }
}
