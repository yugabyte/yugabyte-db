// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.*;
import org.junit.Ignore;
import org.junit.Test;

import java.util.*;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestFrozenType extends BaseCQLTest {

    @Test
    public void testFrozenValues() throws Exception {
        String tableName = "test_frozen";

        String createStmt = String.format("CREATE TABLE %s (h int, r int, " +
                "vm frozen<map<int, text>>, vs frozen<set<text>>, vl frozen<list<double>>," +
                " primary key((h), r));", tableName);
        LOG.info("createStmt: " + createStmt);
        session.execute(createStmt);

        //------------------------------------------------------------------------------------------
        // Testing Insert and Select.
        //------------------------------------------------------------------------------------------
        String insert_template = "INSERT INTO " + tableName + "(h, r, vm, vs, vl) " +
                " VALUES (%d, %d, %s, %s, %s);";

        //---------------------------------- Test Basic Values -----------------------------------\\
        {
            session.execute(String.format(insert_template, 1, 1,
                    "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("r"));

            Map<Integer, String> map_value = row.getMap("vm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("vs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("vl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);
            assertFalse(rows.hasNext());
        }

        //---------------------------------- Test Null Values -----------------------------------\\
        {
            session.execute(String.format(insert_template, 1, 2,
                    "{}", "{ }", "[]"));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 2));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(2, row.getInt("r"));

            Map<Integer, String> map_value = row.getMap("vm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("vs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("vl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        //------------------------------ Test Invalid Insert Stmts -------------------------------\\

        // Wrong collection type (Set instead of Map).
        runInvalidStmt(String.format(insert_template, 1, 1,
                "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

        // Wrong collection type (List instead of Set).
        runInvalidStmt(String.format(insert_template, 1, 1,
                "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]"));

        // Wrong map key type (String instead of Int).
        runInvalidStmt(String.format(insert_template, 1, 1,
                "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]"));

        // Wrong set element type (Int instead of String)
        runInvalidStmt(String.format(insert_template, 1, 1,
                "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]"));

        // Wrong List element type (String instead of Double)
        runInvalidStmt(String.format(insert_template, 1, 1,
                "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']"));

        //------------------------------------------------------------------------------------------
        // Testing Update and Select.
        //------------------------------------------------------------------------------------------
        String update_template = "UPDATE " + tableName + " SET vm = %s, vs = %s, vl = %s WHERE " +
                "h = %d AND r = %d;";

        //---------------------------------- Test Basic Values -----------------------------------\\
        {
            session.execute(String.format(update_template,
                    "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 2));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(2, row.getInt("r"));

            Map<Integer, String> map_value = row.getMap("vm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("vs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("vl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);
            assertFalse(rows.hasNext());
        }

        //----------------------------------- Test Null Values -----------------------------------\\
        {
            session.execute(String.format(update_template, "{}", "{ }", "[]", 1, 1));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d;";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("r"));

            Map<Integer, String> map_value = row.getMap("vm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("vs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("vl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        //------------------------------ Test Invalid Update Stmts -------------------------------\\

        // Wrong collection type (Set instead of Map).
        runInvalidStmt(String.format(update_template,
                "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

        // Wrong collection type (List instead of Set).
        runInvalidStmt(String.format(update_template,
                "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]", 1, 2));

        // Wrong map key type (String instead of Int).
        runInvalidStmt(String.format(update_template,
                "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1, 2));

        // Wrong set element type (Int instead of String)
        runInvalidStmt(String.format(update_template,
                "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]", 1, 2));

        // Wrong List element type (String instead of Double)
        runInvalidStmt(String.format(update_template,
                "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']", 1, 2));
    }

    @Test
    public void testFrozenCollectionsAsKeys() throws Exception {
        String tableName = "test_frozen";

        String createStmt = String.format("CREATE TABLE %s (h int," +
                "rm frozen<map<int, text>>, rs frozen<set<text>>, rl frozen<list<double>>, v int," +
                " primary key((h), rm, rs, rl));", tableName);
        LOG.info("createStmt: " + createStmt);
        session.execute(createStmt);

        //------------------------------------------------------------------------------------------
        // Testing Insert and Select.
        //------------------------------------------------------------------------------------------
        String insert_template = "INSERT INTO " + tableName + "(h, rm, rs, rl, v) " +
                " VALUES (%d, %s, %s, %s, %d);";

        //---------------------------------- Test Basic Values -----------------------------------\\
        {
            session.execute(String.format(insert_template, 1,
                    "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);
            assertFalse(rows.hasNext());
        }

        //---------------------------------- Test Empty Values -----------------------------------\\
        // Empty values for frozen collections are not considered null in CQL => allowed in keys
        {
            session.execute(String.format(insert_template, 2,
                    "{}", "{ }", "[]", 1));

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 2));
            Row row = rows.next();
            assertEquals(2, row.getInt("h"));
            assertEquals(1, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        //------------------------------ Test Invalid Insert Stmts -------------------------------\\

        // Wrong collection type (Set instead of Map).
        runInvalidStmt(String.format(insert_template, 1,
                "{1, 2}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

        // Wrong collection type (List instead of Set).
        runInvalidStmt(String.format(insert_template, 1,
                "{1 : 'a', 2 : 'b'}", "['x', 'y']", "[1.5, 2.5, 3.5]", 1));

        // Wrong map key type (String instead of Int).
        runInvalidStmt(String.format(insert_template, 1,
                "{'1' : 'a', '2' : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));

        // Wrong set element type (Int instead of String)
        runInvalidStmt(String.format(insert_template, 1,
                "{1 : 'a', 2 : 'b'}", "{1, 2}", "[1.5, 2.5, 3.5]", 1));

        // Wrong List element type (String instead of Double)
        runInvalidStmt(String.format(insert_template, 1,
                "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "['1', '2', '3']", 1));

        //------------------------------------------------------------------------------------------
        // Testing Select.
        // CQL allows equality conditions on entire collection values when frozen
        //------------------------------------------------------------------------------------------
        session.execute(String.format(insert_template, 11,
                "{1 : 'a', 2 : 'b'}", "{'x', 'y'}", "[1.5, 2.5, 3.5]", 1));
        session.execute(String.format(insert_template, 11,
                "{}", "{}", "[]", 2));
        String select_template = "SELECT * FROM " + tableName + " WHERE h = 11 AND %s";

        //------------------------------ Testing Map Equality ------------------------------------\\

        // Basic values.
        {
            Iterator<Row> rows =
                    runSelect(String.format(select_template, "rm = {1 : 'a', 2 : 'b'}"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(1, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);

            assertFalse(rows.hasNext());
        }

        // Empty/Null Value.
        {
            Iterator<Row> rows = runSelect(String.format(select_template, "rm = {}"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(2, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        // No Results.
        {
            ResultSet rs = session.execute(String.format(select_template, "rm = {1 : 'a'}"));
            // Checking Rows.
            assertFalse(rs.iterator().hasNext());
        }

        //------------------------------ Testing Set Equality ------------------------------------\\

        // Basic values.
        {
            Iterator<Row> rows =
                    runSelect(String.format(select_template, "rs = {'x', 'y'}"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(1, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);

            assertFalse(rows.hasNext());
        }

        // Empty/Null Value.
        {
            Iterator<Row> rows = runSelect(String.format(select_template, "rs = {}"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(2, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        // No Results.
        {
            ResultSet rs = session.execute(String.format(select_template, "rs = {'y'}"));
            // Checking Rows.
            assertFalse(rs.iterator().hasNext());
        }

        //------------------------------ Testing List Equality -----------------------------------\\
        // Basic Values.
        {
            Iterator<Row> rows =
                    runSelect(String.format(select_template, "rl = [1.5, 2.5, 3.5]"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(1, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(2, map_value.size());
            assertTrue(map_value.containsKey(1));
            assertEquals("a", map_value.get(1));
            assertTrue(map_value.containsKey(2));
            assertEquals("b", map_value.get(2));

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains("x"));
            assertTrue(set_value.contains("y"));

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(3, list_value.size());
            assertEquals(1.5, list_value.get(0), 0.0);
            assertEquals(2.5, list_value.get(1), 0.0);
            assertEquals(3.5, list_value.get(2), 0.0);

            assertFalse(rows.hasNext());
        }

        // Null/Empty Value.
        {
            Iterator<Row> rows = runSelect(String.format(select_template, "rl = []"));
            // Checking Row.
            Row row = rows.next();
            assertEquals(11, row.getInt("h"));
            assertEquals(2, row.getInt("v"));

            Map<Integer, String> map_value = row.getMap("rm", Integer.class, String.class);
            assertEquals(0, map_value.size());

            Set<String> set_value = row.getSet("rs", String.class);
            assertEquals(0, set_value.size());

            List<Double> list_value= row.getList("rl", Double.class);
            assertEquals(0, list_value.size());

            assertFalse(rows.hasNext());
        }

        // No Results.
        {
            ResultSet rs = session.execute(String.format(select_template, "rl = [2.5]"));
            // Checking Rows.
            assertFalse(rs.iterator().hasNext());
        }
    }

    private void createType(String typeName, String... fields) {
        StringBuilder sb = new StringBuilder();
        sb.append("CREATE TYPE ");
        sb.append(typeName);
        sb.append("(");
        boolean first = true;
        for (String field : fields) {
            if (first)
                first = false;
            else
                sb.append(", ");
            sb.append(field);
        }
        sb.append(");");
        String createStmt = sb.toString();
        LOG.info("createType: " + createStmt);
        session.execute(createStmt);
    }

    @Test
    public void testFrozenUDTsInsideCollections() throws Exception {
        String tableName = "test_frozen_nested_col";
        String typeName = "test_udt_employee";
        createType(typeName, "name text", "ssn bigint");

        String createStmt = "CREATE TABLE " + tableName + " (h int, r int, " +
                "v1 set<frozen<map<int, text>>>, v2 list<frozen<test_udt_employee>>," +
                " primary key((h), r));";
        LOG.info("createStmt: " + createStmt);

        session.execute(createStmt);

        // Setup some maps to add into the set
        TypeCodec<Set<Map<Integer, String>>> setCodec =
                TypeCodec.set(TypeCodec.map(TypeCodec.cint(), TypeCodec.varchar()));

        Map<Integer, String> map1 = new HashMap<>();
        map1.put(1, "a");
        map1.put(2, "b");
        Map<Integer, String> map2 = new HashMap<>();
        map2.put(1, "m");
        map2.put(2, "n");
        Map<Integer, String> map3 = new HashMap<>();
        map3.put(11, "x");
        map3.put(12, "y");

        String map1_lit = "{1 : 'a', 2 : 'b'}";
        String map2_lit = "{1 : 'm', 2 : 'n'}";
        String map3_lit = "{11 : 'x', 12 : 'y'}";

        // Setup some user-defined values to add into list
        UserType udt_type = cluster.getMetadata()
                .getKeyspace(DEFAULT_TEST_KEYSPACE)
                .getUserType(typeName);

        UDTValue udt1 = udt_type.newValue()
                .set("name", "John", String.class)
                .set("ssn", 123L, Long.class);
        UDTValue udt2 = udt_type.newValue()
                .set("name", "Jane", String.class)
                .set("ssn", 234L, Long.class);
        UDTValue udt3 = udt_type.newValue()
                .set("name", "Jack", String.class)
                .set("ssn", 321L, Long.class);

        String udt1_lit = "{name : 'John', ssn : 123}";
        String udt2_lit = "{name : 'Jane', ssn : 234}";
        String udt3_lit = "{name : 'Jack', ssn : 321}";

        //------------------------------------------------------------------------------------------
        // Testing Insert
        //------------------------------------------------------------------------------------------
        {
            String insert_template =
                    "INSERT INTO " + tableName + " (h, r, v1, v2) VALUES (%d, %d, %s, %s);";
            session.execute(String.format(insert_template, 1, 1,
                    "{" + map1_lit + ", " + map2_lit + "}",
                    "[" + udt1_lit + ", " + udt2_lit + "]"));
            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("r"));

            Set<Map<Integer, String>> set_value = row.get("v1", setCodec);
            assertEquals(2, set_value.size());
            assertTrue(set_value.contains(map1));
            assertTrue(set_value.contains(map2));

            List<UDTValue> list_value = row.getList("v2", UDTValue.class);
            assertEquals(2, list_value.size());
            assertEquals(udt1, list_value.get(0));
            assertEquals(udt2, list_value.get(1));

            assertFalse(rows.hasNext());
        }

        //------------------------------------------------------------------------------------------
        // Testing Update
        //------------------------------------------------------------------------------------------
        {
            String update_template =
                    "UPDATE " + tableName + " SET v1 = %s WHERE h = %d AND r = %d;";
            session.execute(String.format(update_template,
                    "v1 + {" + map3_lit + "}", 1, 1));
            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("r"));

            Set<Map<Integer, String>> set_value = row.get("v1", setCodec);
            assertEquals(3, set_value.size());
            assertTrue(set_value.contains(map1));
            assertTrue(set_value.contains(map2));
            assertTrue(set_value.contains(map3));

            assertFalse(rows.hasNext());
        }
        {
            String update_template =
                    "UPDATE " + tableName + " SET v2 = %s WHERE h = %d AND r = %d;";
            session.execute(String.format(update_template,
                    "v2 + [" + udt3_lit + "]", 1, 1));
            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 1, 1));
            Row row = rows.next();
            assertEquals(1, row.getInt("h"));
            assertEquals(1, row.getInt("r"));

            List<UDTValue> list_value = row.getList("v2", UDTValue.class);
            assertEquals(3, list_value.size());
            assertTrue(list_value.contains(udt1));
            assertTrue(list_value.contains(udt2));
            assertTrue(list_value.contains(udt3));

            assertFalse(rows.hasNext());
        }

        //------------------------------------------------------------------------------------------
        // Testing Bind
        //------------------------------------------------------------------------------------------
        {
            Set<Map<Integer, String>> set_value = new HashSet<>();
            set_value.add(map1);
            set_value.add(map2);
            set_value.add(map3);

            List<UDTValue> list_value = new LinkedList<>();
            list_value.add(udt1);
            list_value.add(udt2);
            list_value.add(udt3);

            String insert_stmt = "INSERT INTO " + tableName + "(h, r, v1, v2) VALUES (?, ?, ?, ?);";
            session.execute(insert_stmt, 2, 1, set_value, list_value);

            // Checking Row.
            String select_template = "SELECT * FROM " + tableName + " WHERE h = %d AND r = %d";
            Iterator<Row> rows = runSelect(String.format(select_template, 2, 1));
            Row row = rows.next();
            assertEquals(2, row.getInt("h"));
            assertEquals(1, row.getInt("r"));
            assertEquals(set_value, row.get("v1", setCodec));
            assertEquals(list_value, row.getList("v2", UDTValue.class));

            assertFalse(rows.hasNext());
        }
    }
}
