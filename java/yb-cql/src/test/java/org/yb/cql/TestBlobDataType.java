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


import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.*;

import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestBlobDataType extends BaseCQLTest {

    @Test
    public void testValidQueries() throws Exception {
        String tableName = "test_blob_valid";
        createTable(tableName, "blob");

        //------------------------------------------------------------------------------------------
        // Testing Insert
        //------------------------------------------------------------------------------------------
        // using %s for int values to test that literals such as `01` are still parsed as integers
        // since blob literals are of the form `0[xX][0-9A-Fa-f]`
        String insert_template = "INSERT INTO " + tableName + "(h1, h2, r1, r2, v1, v2) " +
                "VALUES (%s, %s, %s, %s, %s, %s)";

        // testing int literals and empty blob values
        String insert_stmt = String.format(insert_template, "1", "0x", "01", "0x", "0001", "0x");
        session.execute(insert_stmt);

        // testing capitalization and null values (i.e. not given for regular columns)
        insert_stmt =  "INSERT INTO " + tableName + "(h1, h2, r1, r2) " +
                "VALUES (1, 0xAbCdEf, 1, 0XaBcDeF)";
        session.execute(insert_stmt);

        // testing blobs containing 0-bytes
        insert_stmt = String.format(insert_template, "001", "0x00ab00", "1", "0X00EF00", "01",
                "0x00cd00FE00");
        session.execute(insert_stmt);

        // testing large values -- 1000 * 16 characters -> 8000 bytes internally
        String long_hex = String.join("", Collections.nCopies(1000, "0123456789abcdef"));
        insert_stmt = String.format(insert_template, "1", "0xab" + long_hex, "1", "0xcd" + long_hex,
                "1", "0xef" + long_hex);
        session.execute(insert_stmt);

        //------------------------------------------------------------------------------------------
        // Testing Select
        //------------------------------------------------------------------------------------------
        String select_template = "SELECT * FROM " + tableName + " WHERE h1 = %d AND h2 = %s AND " +
                "r1 = %d AND r2 = %s";

        // selecting for empty keys
        String select_stmt = String.format(select_template, 1, "0x", 1, "0x");
        Row row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0x", makeBlobString(row.getBytes("h2")));
        assertEquals("0x", makeBlobString(row.getBytes("r2")));
        assertEquals("0x", makeBlobString(row.getBytes("v2")));

        // selecting for capitalized input (different from insert one) and null values
        select_stmt = String.format(select_template, 1, "0XaBcDeF", 1, "0xAbCdEf");
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(0, row.getInt("v1"));
        // output is normalized to lowercase
        assertEquals("0xabcdef", makeBlobString(row.getBytes("h2")));
        assertEquals("0xabcdef", makeBlobString(row.getBytes("r2")));
        assertEquals(null, row.getBytes("v2"));

        // select blobs containing \0 bytes (capitalization is normalized to lowercase)
        select_stmt = String.format(select_template, 1, "0x00ab00", 1, "0x00ef00");
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0x00ab00", makeBlobString(row.getBytes("h2")));
        assertEquals("0x00ef00", makeBlobString(row.getBytes("r2")));
        assertEquals("0x00cd00fe00", makeBlobString(row.getBytes("v2")));

        // select with long keys
        select_stmt = String.format(select_template, 1, "0xab" + long_hex, 1, "0xcd" + long_hex);
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0xab" + long_hex, makeBlobString(row.getBytes("h2")));
        assertEquals("0xcd" + long_hex, makeBlobString(row.getBytes("r2")));
        assertEquals("0xef" + long_hex, makeBlobString(row.getBytes("v2")));

        //------------------------------------------------------------------------------------------
        // Testing Update
        //------------------------------------------------------------------------------------------
        String update_template = "UPDATE " + tableName + " SET %s = %s WHERE h1 = 1 AND h2 = %s " +
                " AND r1 = 1 AND r2 = %s";

        // update for empty keys
        String update_stmt = String.format(update_template, "v2", "0x0f", "0x", "0x");
        session.execute(update_stmt);
        // checking row
        select_stmt = String.format(select_template, 1, "0x", 1, "0x");
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0x", makeBlobString(row.getBytes("h2")));
        assertEquals("0x", makeBlobString(row.getBytes("r2")));
        assertEquals("0x0f", makeBlobString(row.getBytes("v2"))); // new value

        // updating blobs with \0 bytes
        update_stmt = String.format(update_template, "v2", "0x00000000", "0x00ab00", "0x00ef00");
        session.execute(update_stmt);
        // checking row
        select_stmt = String.format(select_template, 1, "0x00ab00", 1, "0x00ef00");
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0x00ab00", makeBlobString(row.getBytes("h2")));
        assertEquals("0x00ef00", makeBlobString(row.getBytes("r2")));
        assertEquals("0x00000000", makeBlobString(row.getBytes("v2"))); // new value

        // update for large keys
        update_stmt = String.format(update_template, "v2", "0x42", "0xab" + long_hex,
                "0xcd" + long_hex);
        session.execute(update_stmt);
        // checking row
        select_stmt = String.format(select_template, 1, "0xab" + long_hex, 1, "0xcd" + long_hex);
        row = runSelect(select_stmt).next();
        assertEquals(1, row.getInt("h1"));
        assertEquals(1, row.getInt("r1"));
        assertEquals(1, row.getInt("v1"));
        assertEquals("0xab" + long_hex, makeBlobString(row.getBytes("h2")));
        assertEquals("0xcd" + long_hex, makeBlobString(row.getBytes("r2")));
        assertEquals("0x42", makeBlobString(row.getBytes("v2"))); // new value

        dropTable(tableName);
    }

    @Test
    public void testOrdering() throws Exception {
        String create_table_template = "CREATE TABLE %s (h int, r blob, v int, primary key(h, r))" +
                " WITH CLUSTERING ORDER BY(r %s)";
        String insert_template = "INSERT INTO %s (h, r, v) VALUES (1, %s, 1)";
        String select_template = "SELECT h, r, v FROM %s WHERE h = 1";


        // constructing an ordered list of blobs
        List<String> blobs = new LinkedList<>();

        for (long i = 0; i < 20; i++) {
            // generating some non-trivial seed values
            ByteBuffer buf = makeByteBuffer(i * 1000000000L + 23679 * i * i);
            blobs.add(makeBlobString(buf));
        }

        //---------------------------------- testing ascending -----------------------------------\\
        String tableNameAsc = "test_blob_ascending";
        session.execute(String.format(create_table_template, tableNameAsc, "ASC"));
        for (int i = 0; i < 20; i++) {
            // "randomize" insert order (covers all indexes since 11 & 20 are co-prime)
            String current_blob = blobs.get((i * 11) % 20);
            session.execute(String.format(insert_template, tableNameAsc, current_blob));
        }
        String select_stmt = String.format(select_template, tableNameAsc);

        ResultSet rs = session.execute(select_stmt);
        assertEquals(20, rs.getAvailableWithoutFetching());

        // Rows should come sorted by column r in ascending order.
        for (int i = 0; i < 20; i++) {
            Row row = rs.one();
            assertEquals(1, row.getInt("h"));
            assertEquals(blobs.get(i), makeBlobString(row.getBytes("r")));
            assertEquals(1, row.getInt("v"));
        }

        dropTable(tableNameAsc);

        //---------------------------------- testing descending ----------------------------------\\
        String tableNameDesc = "test_blob_descending";
        session.execute(String.format(create_table_template, tableNameDesc, "DESC"));

        for (int i = 0; i < 20; i++) {
            // "randomize" insert order (covers all indexes since 13 & 20 are co-prime)
            String current_blob = blobs.get((i * 13) % 20);
            session.execute(String.format(insert_template, tableNameDesc, current_blob));
        }
        select_stmt = String.format(select_template, tableNameDesc);
        rs = session.execute(select_stmt);
        assertEquals(20, rs.getAvailableWithoutFetching());

        // Rows should come sorted by column r in descending order.
        for (int i = 19; i >= 0; i--) {
            Row row = rs.one();
            assertEquals(1, row.getInt("h"));
            assertEquals(blobs.get(i), makeBlobString(row.getBytes("r")));
            assertEquals(1, row.getInt("v"));
        }

        dropTable(tableNameDesc);
    }

    @Test
    public void testInValidQueries() throws Exception {
        String tableName = "test_blob_valid";
        createTable(tableName, "blob");

        // testing mostly parsing here so negative tests use just inserts
        String insert_template = "INSERT INTO " + tableName + "(h1, h2, r1, r2, v1, v2) " +
                "VALUES (%d, %s, %d, %s, %d, %s)";

        //-------------- testing odd number of characters (unfinished last-byte) -----------------\\
        // incomplete value in hash key
        String insert_stmt = String.format(insert_template, 1, "0xa", 1, "0x", 1, "0x");
        runInvalidStmt(insert_stmt);
        // incomplete value in range key
        insert_stmt = String.format(insert_template, 1, "0xab", 1, "0xeff", 1, "0xcd");
        runInvalidStmt(insert_stmt);
        // incomplete value in regular column
        insert_stmt = String.format(insert_template, 1, "0xab", 1, "0xef", 1, "0xcdefg");
        runInvalidStmt(insert_stmt);

        //------------------------ testing non-hex characters in input ---------------------------\\
        // invalid letter (in hash key)
        insert_stmt = String.format(insert_template, 1, "0xag", 1, "0xcd", 1, "0xef");
        runInvalidStmt(insert_stmt);
        // invalid character (in range key)
        insert_stmt = String.format(insert_template, 1, "0xab", 1, "0xe!", 1, "0xcdefg");
        runInvalidStmt(insert_stmt);
        // invalid character (in regular column)
        insert_stmt = String.format(insert_template, 1, "0xab", 1, "0xef", 1, "0zcdab");
        runInvalidStmt(insert_stmt);

        dropTable(tableName);
    }

}
