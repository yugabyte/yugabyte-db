// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.Row;

import org.junit.Test;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestUpdate extends TestBase {

    @Test
    public void testUpdateWithTTL() throws Exception {
        String tableName = "test_update_with_ttl";
        CreateTable(tableName);

        // Insert a row.
        String insert_stmt = String.format(
          "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
          tableName, 1, 2, 3, 4, 5, 6);
        session.execute(insert_stmt);

        // Update v1 with a TTL.
        String update_stmt = String.format(
          "UPDATE %s USING TTL 1000 SET v1 = 500 WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
            "r2 = 'r4';",
          tableName);
        session.execute(update_stmt);

        // Update v2 with a TTL.
        update_stmt = String.format(
          "UPDATE %s USING TTL 2000 SET v2 = 'v600' WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
            "r2 = 'r4';",
          tableName);
        session.execute(update_stmt);

        String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s" +
          "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

        // Verify row is present.
        Row row = RunSelect(tableName, select_stmt).next();
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(3, row.getInt(2));
        assertEquals("r4", row.getString(3));
        assertEquals(500, row.getInt(4));
        assertEquals("v600", row.getString(5));

        // Now verify v1 expires.
        Thread.sleep(1100);
        row = RunSelect(tableName, select_stmt).next();
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(3, row.getInt(2));
        assertEquals("r4", row.getString(3));
        assertTrue(row.isNull(4));
        assertEquals("v600", row.getString(5));

        // Now verify v2 expires.
        Thread.sleep(1000);
        row = RunSelect(tableName, select_stmt).next();
        assertEquals(1, row.getInt(0));
        assertEquals("h2", row.getString(1));
        assertEquals(3, row.getInt(2));
        assertEquals("r4", row.getString(3));
        assertTrue(row.isNull(4));
        assertTrue(row.isNull(5));
    }
}
