package org.yb.cql;

import com.datastax.driver.core.Row;
import org.junit.Test;

import java.util.Iterator;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestBooleanDataType extends BaseCQLTest {

  @Test
  public void testComparison() throws Exception {
    // Setup table
    session.execute("create table bool_test(h int, v boolean, primary key(h))");
    session.execute("insert into bool_test(h, v) values (1, true)");
    session.execute("insert into bool_test(h, v) values (2, false)");

    // Check boolean equality: without hash condition.
    Iterator<Row> rows = runSelect("SELECT v FROM bool_test WHERE v = true");
    // Expecting one row, with value 'true'.
    assertTrue(rows.hasNext());
    assertTrue(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean equality: with hash condition.
    rows = runSelect("SELECT v FROM bool_test WHERE h = 2 AND v = false");
    // Expecting one row, with value 'false'.
    assertTrue(rows.hasNext());
    assertFalse(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean comparison: less than.
    rows = runSelect("SELECT v FROM bool_test WHERE v < true");
    // Expecting one row, with value 'false'.
    assertTrue(rows.hasNext());
    assertFalse(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean comparison: greater or equal.
    rows = runSelect("SELECT v FROM bool_test WHERE v >= false");
    // Expecting two rows, 'true' and 'false', in any order.
    assertTrue(rows.hasNext());
    boolean first = rows.next().getBool("v");
    assertTrue(rows.hasNext());
    boolean second = rows.next().getBool("v");
    assertNotEquals(first, second);
    assertFalse(rows.hasNext());

    // Boolean type is not yet allowed in the primary key.
    // TODO When we enable booleans in the key we should replace these with proper tests as above.
    runInvalidStmt("create table bool_test(h boolean, v int, primary key(h))");
    runInvalidStmt("create table bool_test(h int, r boolean, v int, primary key(h, r))");
  }
}
