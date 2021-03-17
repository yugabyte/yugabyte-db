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
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.math.BigInteger;
import java.util.Iterator;
import java.util.Random;
import java.util.TreeSet;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestVarIntDataType extends BaseCQLTest {
    private static final Logger LOG = LoggerFactory.getLogger(TestVarIntDataType.class);

    private String getRandomVarInt(boolean withSign, int length) {
        String digits = "0123456789";
        final Random random = new Random();

        String s = "";
        for (int j = 0; j < length; j++) {
            s += digits.charAt(random.nextInt(digits.length() - 1));
        }

        if (withSign) {
            int i = random.nextInt(4);
            if (i < 2) {
                // Half of the time make this a negative number.
                s = "-" + s;
            } else if (i == 3) {
                // 25% of the time append a '+' sign to test parsing.
                s = "+" + s;
            }
        }

        return s;
    }

    private String getRandomVarInt(boolean withSign) {
        final Random random = new Random();
        int length = random.nextInt(100) + 20;
        return getRandomVarInt(withSign, length);
    }

    private String getRandomVarInt() {
        return getRandomVarInt(true);
    }

    @Test
    public void testVarIntDataTypeInHash() throws Exception {
        BigInteger hash = new BigInteger("-2");
        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();
        varints.add(new BigInteger("-1"));
        varints.add(new BigInteger("1"));
        varints.add(new BigInteger("12536754382939687593029374643822974729010012837467"));
        varints.add(new BigInteger("-636546372800009887665589028645748390938391013"));
        varints.add(new BigInteger("-6"));
        varints.add(new BigInteger("-599999956"));
        varints.add(new BigInteger("-58999999"));
        varints.add(new BigInteger("-12"));
        varints.add(new BigInteger("-115"));
        varints.add(new BigInteger("-05"));
        varints.add(new BigInteger("0"));
        varints.add(new BigInteger("5677"));
        varints.add(new BigInteger("1058986"));
        varints.add(new BigInteger("115777675644645342392"));
        varints.add(new BigInteger("-887875512"));
        testVarIntDataTypeInHash(hash, varints);
    }

    @Test
    public void testVarIntDataTypeInHashRandom() throws Exception {
        final Random random = new Random();
        BigInteger hashVarInt = new BigInteger(getRandomVarInt());
        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();
        for (int i = 0; i < 100; i++) {
            BigInteger varint;
            do {
                varint = new BigInteger(getRandomVarInt());
            } while (!varints.add(varint));
        }

        testVarIntDataTypeInHash(hashVarInt, varints);
    }

    private void testVarIntDataTypeInHash(BigInteger hashVarInt, TreeSet<BigInteger> varints)
            throws Exception {
        LOG.info("TEST CQL VARINT TYPE IN HASH - Start");

        // Create table
        String tableName = "test_varint";
        String createStmt = String.format("CREATE TABLE %s " +
                "(h1 varint, h2 int, r1 varint, r2 int, v1 varint, v2 int, " +
                "primary key((h1, h2), r1, r2));", tableName);
        session.execute(createStmt);


        for (BigInteger varint : varints) {
            // Insert one row. Deliberately insert with same hash key
            // but different range column values.
            final String insertStmt = String.format("INSERT INTO %s (h1, h2, r1, r2, v1, v2) " +
                    "VALUES (%s, 1, %s, %d, %s, 2);",
                    tableName, hashVarInt.toString(), varint.toString(), varint.intValue(),
                    varint.toString());
            LOG.info("insertStmt: " + insertStmt);
            session.execute(insertStmt);
        }

        // Select row by the hash key. Results should come sorted by range keys in ascending order.
        final String selectStmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
                "WHERE h1 = %s AND h2 = 1;", tableName, hashVarInt.toString());
        LOG.info("selectStmt: " + selectStmt);
        ResultSet rs = session.execute(selectStmt);
        assertEquals(varints.size(), rs.getAvailableWithoutFetching());

        BigInteger sumVarint = new BigInteger("0");

        for (Iterator<BigInteger> iter = varints.iterator(); iter.hasNext();) {
            Row row = rs.one();
            BigInteger varint = iter.next();

            assertEquals(hashVarInt, row.getVarint("h1"));
            assertEquals(1, row.getInt("h2"));
            assertEquals(varint, row.getVarint("r1"));
            assertEquals(varint.intValue(), row.getInt("r2"));
            assertEquals(varint, row.getVarint("v1"));
            assertEquals(2, row.getInt("v2"));
            sumVarint = sumVarint.add(varint);
        }

        // Select sum of rows by the hash key. Should be 1 result row.
        final String selectSumStmt = String.format("SELECT sum(v1) FROM %s " +
                "WHERE h1 = %s AND h2 = 1;", tableName, hashVarInt.toString());
        LOG.info("selectSumStmt: " + selectSumStmt);
        ResultSet rsSum = session.execute(selectSumStmt);
        assertEquals(1, rsSum.getAvailableWithoutFetching());

        Row rowSum = rsSum.one();

        assertEquals(sumVarint, rowSum.getVarint(0));

        // Test UPDATE with hash and range decimal keys.
        for (Iterator<BigInteger> iter = varints.iterator(); iter.hasNext();) {
            BigInteger rangeVarInt = iter.next();

            BigInteger newVarIntValue = new BigInteger(getRandomVarInt());
            final String updateStmt = String.format("UPDATE %s SET v1 = %s " +
                    "WHERE h1 = %s AND h2 = 1 and r1 = %s and r2 = %d",
                    tableName, newVarIntValue.toString(), hashVarInt.toString(),
                    rangeVarInt.toString(), rangeVarInt.intValue());
            rs = session.execute(updateStmt);

            final String selectStmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
                    "WHERE h1 = %s AND h2 = 1 AND r1 = %s and r2 = %d;",
                    tableName, hashVarInt.toString(),
                    rangeVarInt.toString(), rangeVarInt.intValue());
            rs = session.execute(selectStmt3);
            assertEquals(1, rs.getAvailableWithoutFetching());

            Row row = rs.one();

            assertEquals(hashVarInt, row.getVarint("h1"));
            assertEquals(1, row.getInt("h2"));
            assertEquals(rangeVarInt, row.getVarint("r1"));
            assertEquals(rangeVarInt.intValue(), row.getInt("r2"));
            assertEquals(newVarIntValue, row.getVarint("v1"));
            assertEquals(2, row.getInt("v2"));
        }

        // Test DELETE with hash and range decimal keys.
        for (Iterator<BigInteger> iter = varints.iterator(); iter.hasNext();) {
            BigInteger rangeVarInt = iter.next();

            final String deleteStmt =
                    String.format("DELETE FROM %s WHERE h1 = %s AND h2 = 1 and r1 = %s and r2 = %d",
                            tableName, hashVarInt.toString(), rangeVarInt.toString(),
                            rangeVarInt.intValue());
            rs = session.execute(deleteStmt);

            final String selectStmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s " +
                    "WHERE h1 = %s AND h2 = 1 AND r1 = %s and r2 = %d;",
                    tableName, hashVarInt.toString(),
                    rangeVarInt.toString(), rangeVarInt.intValue());
            rs = session.execute(selectStmt3);
            assertEquals(0, rs.getAvailableWithoutFetching());
        }

        final String dropStmt = "DROP TABLE test_varint;";
        session.execute(dropStmt);
        LOG.info("TEST CQL VARINT TYPE IN HASH - End");
    }

    private void varintDataTypeInRange(TreeSet<BigInteger> varints,
                                        boolean sortIsAscending) throws Exception {
        String sortOrder = "ASC";
        if (!sortIsAscending) {
            sortOrder = "DESC";
        }
        // Create table
        String createStmt = String.format("CREATE TABLE test_varint " +
                    "(h1 varchar, r1 varint, v1 int, primary key(h1, r1)) " +
                    "WITH CLUSTERING ORDER BY (r1 %s);",
                    sortOrder);
        session.execute(createStmt);

        for (BigInteger varint : varints) {
            // Insert one row. Deliberately insert with same hash key
            // but different range column values.
            final String insertStmt = String.format("INSERT INTO test_varint (h1, r1, v1) " +
                    "VALUES ('bob', %s, 1);", varint.toString());
            LOG.info("insertStmt: " + insertStmt);
            session.execute(insertStmt);
        }

        final String selectStmt = "SELECT h1, r1, v1 FROM test_varint WHERE h1 = 'bob';";

        ResultSet rs = session.execute(selectStmt);
        assertEquals(varints.size(), rs.getAvailableWithoutFetching());

        // Verify data is sorted as expected.
        Iterator<BigInteger> iter;
        if (sortIsAscending) {
            iter = varints.iterator();
        } else {
            iter = varints.descendingIterator();
        }
        while (iter.hasNext()) {
            Row row = rs.one();
            BigInteger nextVarInt = iter.next();
            assertEquals(nextVarInt, row.getVarint("r1"));
        }

        final String dropStmt = "DROP TABLE test_varint;";
        session.execute(dropStmt);
    }

    private TreeSet<BigInteger> getRandomVarIntSet() {
        final Random random = new Random();
        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();
        for (int i = 0; i < 100; i++) {
            BigInteger varint;
            do {
                varint = new BigInteger(getRandomVarInt());
            } while (!varints.add(varint));
        }
        return varints;
    }

    @Test
    public void testAscendingVarIntDataTypeInRangeRandom() throws Exception {
        LOG.info("TEST CQL RANDOM ASCENDING VARINT TYPE IN RANGE - Start");
        varintDataTypeInRange(getRandomVarIntSet(), true);
        LOG.info("TEST CQL RANDOM ASCENDING VARINT TYPE IN RANGE - End");
    }

    @Test
    public void testDescendingVarIntDataTypeInRangeRandom() throws Exception {
        LOG.info("TEST CQL RANDOM DESCENDING VARINT TYPE IN RANGE - Start");
        varintDataTypeInRange(getRandomVarIntSet(), false);
        LOG.info("TEST CQL RANDOM DESCENDING VARINT TYPE IN RANGE - End");
    }

    private TreeSet<BigInteger> getVarIntSet() {
        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();
        varints.add(new BigInteger("-90987677556544342116798008765443257900865466645"));
        varints.add(new BigInteger("-1354657678656357866789867547547685"));
        varints.add(new BigInteger("-65654334655453765786765"));
        varints.add(new BigInteger("-53452321455564764"));
        varints.add(new BigInteger("-12216790866"));
        varints.add(new BigInteger("-6565655"));
        varints.add(new BigInteger("-3412"));
        varints.add(new BigInteger("-129"));
        varints.add(new BigInteger("-128"));
        varints.add(new BigInteger("-127"));
        varints.add(new BigInteger("-1"));
        varints.add(new BigInteger("0"));
        varints.add(new BigInteger("1"));
        varints.add(new BigInteger("127"));
        varints.add(new BigInteger("128"));
        varints.add(new BigInteger("129"));
        varints.add(new BigInteger("32112"));
        varints.add(new BigInteger("1345656655"));
        varints.add(new BigInteger("89889867551200090990"));
        varints.add(new BigInteger("65654334199853765781930985"));
        varints.add(new BigInteger("1614657678656357812389867547547005"));
        varints.add(new BigInteger("1233455667788900009887766443212568909875468985432" +
                                        "3567890711122345663214580780475870417483284758"));
        return varints;
    }

    @Test
    public void testAscendingVarIntDataTypeInRange() throws Exception {
        LOG.info("TEST CQL ASCENDING VARINT TYPE IN RANGE - Start");
        varintDataTypeInRange(getVarIntSet(), true);
        LOG.info("TEST CQL ASCENDING VARINT TYPE IN RANGE - End");
    }

    @Test
    public void testDescendingVarIntDataTypeInRange() throws Exception {
        LOG.info("TEST CQL DESCENDING VARINT TYPE IN RANGE - Start");
        varintDataTypeInRange(getVarIntSet(), false);
        LOG.info("TEST CQL DESCENDING VARINT TYPE IN RANGE - End");
    }

    @Test
    public void testVarIntComparisonInRange() throws Exception {
        LOG.info("TEST CQL VARINT TYPE IN RANGE - Start");

        // Create table
        String createStmt = "CREATE TABLE test_varint" +
                "(h1 varchar, r1 varint, r2 varint, v1 int, primary key(h1, r1, r2));";
        session.execute(createStmt);

        final Random random = new Random();
        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();
        for (int i = 0; i < 100; i++) {
            BigInteger varint;
            do {
                varint = new BigInteger(getRandomVarInt());
            } while (!varints.add(varint));

            // Insert one row. Deliberately insert with same hash key
            // but different range column values.
            final String insertStmt =
                    String.format("INSERT INTO test_varint (h1, r1, r2, v1) " +
                            "VALUES ('bob', %s, -100166, 1);", varint.toString());
            LOG.info("insertStmt: " + insertStmt);
            session.execute(insertStmt);
        }

        int i = 1;
        for (Iterator<BigInteger> iter = varints.iterator(); iter.hasNext(); i++) {
            BigInteger varint = iter.next();
            // Select rows that are greater than a specific decimal.
            final String selectStmt =
                    String.format("SELECT h1, r1, v1 FROM test_varint " +
                            "WHERE h1 = 'bob' AND r1 > %s;", varint.toString());
            LOG.info("selectStmt: " + selectStmt);
            ResultSet rs = session.execute(selectStmt);
            LOG.info("got " + rs.getAvailableWithoutFetching() + " results");
            assertEquals(varints.size() - i, rs.getAvailableWithoutFetching());
        }

        i = 1;
        for (Iterator<BigInteger> iter = varints.descendingIterator(); iter.hasNext(); i++) {
            BigInteger varint = iter.next();
            // Select rows that are greater than a specific decimal.
            final String selectStmt =
                    String.format("SELECT h1, r1, v1 FROM test_varint " +
                            "WHERE h1 = 'bob' AND r1 < %s;", varint.toString());
            LOG.info("selectStmt: " + selectStmt);
            ResultSet rs = session.execute(selectStmt);
            LOG.info("got " + rs.getAvailableWithoutFetching() + " results");
            assertEquals(varints.size() - i, rs.getAvailableWithoutFetching());
        }

        final String dropStmt = "DROP TABLE test_varint;";
        session.execute(dropStmt);
        LOG.info("TEST CQL VARINT TYPE IN RANGE - End");
    }

    @Test
    public void testVarIntMultipleComparisonInRange() throws Exception {
        BigInteger varint1 = new BigInteger("-178786754312346800990099876544567876543212356322");
        BigInteger varint2 = new BigInteger("3990021135423556678909734146884247680097435789421584");
        BigInteger delta = new BigInteger("5");

        LOG.info("TEST CQL VARINT TYPE IN RANGE - Start");
        testVarIntMultipleComparisonInRange(varint1, varint2, delta);
        LOG.info("TEST CQL VARINT TYPE IN RANGE - End");
    }

    @Test
    public void testVarIntMultipleComparisonInRangeRandom() throws Exception {
        final Random random = new Random();
        BigInteger varint1 = new BigInteger(getRandomVarInt());
        varint1 = varint1.multiply(BigInteger.TEN).add(BigInteger.ONE);
        BigInteger varint2 = new BigInteger(getRandomVarInt());
        varint2 = varint2.multiply(BigInteger.TEN).subtract(BigInteger.ONE);
        BigInteger delta = new BigInteger("1");

        LOG.info("TEST CQL VARINT TYPE IN RANGE RANDOM - Start");
        testVarIntMultipleComparisonInRange(varint1, varint2, delta);
        LOG.info("TEST CQL VARINT TYPE IN RANGE RANDOM - End");
    }

    private void testVarIntMultipleComparisonInRange(BigInteger varint1, BigInteger varint2,
                                                     BigInteger delta) throws Exception {
        // Create table
        String createStmt = "CREATE TABLE test_varint" +
                "(h1 varchar, r1 varint, r2 varint, v1 int, primary key(h1, r1, r2));";
        session.execute(createStmt);

        final String insertStmt =
                String.format("INSERT INTO test_varint (h1, r1, r2, v1) " +
                        "VALUES ('bob', %s, %s, 1);", varint1.toString(), varint2.toString());
        LOG.info("insertStmt: " + insertStmt);
        session.execute(insertStmt);

        BigInteger smallerVarInt1 = varint1.subtract(delta);
        BigInteger smallerVarInt2 = varint2.subtract(delta);
        BigInteger largerVarInt1 = varint1.add(delta);
        BigInteger largerVarInt2 = varint2.add(delta);

        String selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_varint " +
                        "WHERE h1 = 'bob' AND r1 > %s AND r2 < %s;", smallerVarInt1.toString(),
                largerVarInt2.toString());

        ResultSet rs = session.execute(selectStmt);
        assertEquals(1, rs.getAvailableWithoutFetching());

        selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_varint " +
                        "WHERE h1 = 'bob' AND r1 < %s AND r2 > %s;", largerVarInt1.toString(),
                smallerVarInt2.toString());

        rs = session.execute(selectStmt);
        assertEquals(1, rs.getAvailableWithoutFetching());

        selectStmt = String.format("SELECT h1, r1, r2, v1 FROM test_varint " +
                        "WHERE h1 = 'bob' AND r1 > %s AND r2 < %s;", largerVarInt1.toString(),
                smallerVarInt2.toString());

        rs = session.execute(selectStmt);
        assertEquals(0, rs.getAvailableWithoutFetching());

        selectStmt =  String.format("SELECT h1, r1, r2, v1 FROM test_varint " +
                        "WHERE h1 = 'bob' AND r1 < %s AND r2 > %s;", smallerVarInt1.toString(),
                largerVarInt2.toString());

        rs = session.execute(selectStmt);
        assertEquals(0, rs.getAvailableWithoutFetching());

        final String dropStmt = "DROP TABLE test_varint;";
        session.execute(dropStmt);
    }


    @Test
    public void testConversionsLimits() throws Exception {
        // Test the numeric data types limits. This process includes conversions from varint ->
        // (tinyint, smallint, int, bigint, decimal, double, float).
        LOG.info("TEST CQL CONVERSIONS LIMITS - Start");

        String createStmt = "CREATE TABLE test_varint" +
                "(h1 varint, r1 varint, v1 tinyint, v2 smallint, v3 int, v4 bigint, " +
                "v5 float, v6 double, primary key(h1, r1));";
        session.execute(createStmt);

        TreeSet<BigInteger> varints = new TreeSet<BigInteger>();

        BigInteger varintHash;

        // Create a unique varint hash.
        do {
            varintHash = new BigInteger(getRandomVarInt());
        } while (!varints.add(varintHash));

        // Test the minimum values allowed for each integer type.
        final String insertStmtFmt =
                "INSERT INTO test_varint (h1, r1, v1, v2, v3, v4, v5, v6) " +
                        "VALUES (%s, 1, %d, %d, %d, %d, %e, %e);";

        String insertStmt = String.format(insertStmtFmt, varintHash.toString(), -128, -32768,
                Integer.MIN_VALUE, Long.MIN_VALUE, Float.MIN_VALUE,
                Double.MIN_VALUE);
        session.execute(insertStmt);

        final String selectStmtFmt =
                "SELECT h1, v1, v2, v3, v4, v5, v6 FROM test_varint WHERE h1 = %s;";
        String selectStmt = String.format(selectStmtFmt, varintHash.toString());
        LOG.info("selectStmt: " + selectStmt);

        ResultSet rs = session.execute(selectStmt);
        assertEquals(1, rs.getAvailableWithoutFetching());

        Row row = rs.one();
        assertEquals(varintHash, row.getVarint("h1"));
        assertEquals(-128, row.getByte("v1"));
        assertEquals(-32768, row.getShort("v2"));
        assertEquals(Integer.MIN_VALUE, row.getInt("v3"));
        assertEquals(Long.MIN_VALUE, row.getLong("v4"));
        assertEquals(Float.MIN_VALUE, row.getFloat("v5"), Float.MIN_VALUE);
        assertEquals(Double.MIN_VALUE, row.getDouble("v6"), Double.MIN_VALUE);

        // Test the maximum values allowed for each integer type.
        insertStmt = String.format(insertStmtFmt, varintHash.toString(), 127, 32767,
                Integer.MAX_VALUE, Long.MAX_VALUE, Float.MAX_VALUE, Double.MAX_VALUE);
        session.execute(insertStmt);

        selectStmt = String.format(selectStmtFmt, varintHash.toString());

        rs = session.execute(selectStmt);
        assertEquals(1, rs.getAvailableWithoutFetching());

        row = rs.one();
        assertEquals(varintHash, row.getVarint("h1"));
        assertEquals(127, row.getByte("v1"));
        assertEquals(32767, row.getShort("v2"));
        assertEquals(Integer.MAX_VALUE, row.getInt("v3"));
        assertEquals(Long.MAX_VALUE, row.getLong("v4"));
        assertEquals(Float.MAX_VALUE, row.getFloat("v5"), Float.MAX_VALUE / 1e5);
        assertEquals(Double.MAX_VALUE, row.getDouble("v6"), Double.MAX_VALUE / 1e5);

        final String dropStmt = "DROP TABLE test_varint;";
        session.execute(dropStmt);

        LOG.info("TEST CQL CONVERSIONS LIMITS - End");
    }
}
