package org.yb.cql;

import java.net.InetAddress;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;

import org.junit.Test;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

public class TestSystemTables extends TestBase {

  private void verifyPeersTable(ResultSet rs, InetAddress expected) throws Exception {
    assertEquals(1, rs.all().size());
    for (Row row : rs.all()) {
      assertEquals(expected, row.getInet(0)); // peer
      assertEquals(expected, row.getInet(6)); // rpc address
    }
  }

  private boolean verifySystemSchemaTables(List<Row> results,
                                           String namespace_name,
                                           String table_name) {
    for (Row row : results) {
      if (row.getString("keyspace_name").equals(namespace_name)
        && row.getString("table_name") == table_name) {
        return true;
      }
    }
    return false;
  }

  @Test
  public void testSystemPeersTable() throws Exception {
    // Pick only 1 contact point since all will have same IP.
    InetAddress contactPoint = miniCluster.getCQLContactPoints().get(0).getAddress();

    ResultSet rs = session.execute("SELECT * FROM system.peers;");
    verifyPeersTable(rs, contactPoint);

    // Try with where clause.
    rs = session.execute(String.format("SELECT * FROM system.peers WHERE peer = '%s'",
      contactPoint.getHostAddress()));
    verifyPeersTable(rs, contactPoint);

    rs = session.execute("SELECT * FROM system.peers WHERE peer = '127.0.0.2'");
    assertEquals(0, rs.all().size());
  }

  @Test
  public void testEmptySystemTables() throws Exception {
    // Tables should be empty.
    assertEquals(0, session.execute("SELECT * FROM system_schema.aggregates;").all().size());
    assertEquals(0, session.execute("SELECT * FROM system_schema.functions;").all().size());
    assertEquals(0, session.execute("SELECT * FROM system_schema.indexes;").all().size());
    assertEquals(0, session.execute("SELECT * FROM system_schema.triggers;").all().size());
    assertEquals(0, session.execute("SELECT * FROM system_schema.types;").all().size());
    assertEquals(0, session.execute("SELECT * FROM system_schema.views;").all().size());
  }

  @Test
  public void testSystemKeyspacesAndTables() throws Exception {
    List <Row> results = session.execute(
      "SELECT * FROM system_schema.keyspaces;").all();
    assertEquals(3, results.size());
    assertEquals("system_schema", results.get(0).getString("keyspace_name"));
    assertTrue(results.get(0).getBool("durable_writes"));
    assertEquals("system", results.get(1).getString("keyspace_name"));
    assertTrue(results.get(1).getBool("durable_writes"));
    assertEquals(DEFAULT_KEYSPACE, results.get(2).getString("keyspace_name"));
    assertTrue(results.get(2).getBool("durable_writes"));

    results = session.execute(
      "SELECT * FROM system_schema.tables;").all();
    assertEquals(10, results.size());
    verifySystemSchemaTables(results, "system_schema", "aggregates");
    verifySystemSchemaTables(results, "system_schema", "columns");
    verifySystemSchemaTables(results, "system_schema", "functions");
    verifySystemSchemaTables(results, "system_schema", "indexes");
    verifySystemSchemaTables(results, "system_schema", "triggers");
    verifySystemSchemaTables(results, "system_schema", "types");
    verifySystemSchemaTables(results, "system_schema", "views");
    verifySystemSchemaTables(results, "system_schema", "keyspaces");
    verifySystemSchemaTables(results, "system_schema", "tables");
    verifySystemSchemaTables(results, "system", "peers");

    // Create keyspace and table and verify it shows up.
    session.execute("CREATE KEYSPACE my_keyspace;");
    session.execute("CREATE TABLE my_table (c1 int PRIMARY KEY);");
    session.execute("CREATE TABLE my_keyspace.my_table (c1 int PRIMARY KEY);");

    // Verify results.
    results = session.execute(
      "SELECT keyspace_name, durable_writes FROM system_schema.keyspaces WHERE keyspace_name = " +
        "'my_keyspace';").all();
    assertEquals(1, results.size());
    assertEquals("my_keyspace", results.get(0).getString("keyspace_name"));
    assertTrue(results.get(0).getBool("durable_writes"));

    results = session.execute(
      String.format("SELECT keyspace_name, table_name, flags FROM system_schema.tables WHERE " +
        "keyspace_name = " +
        "'%s' and table_name = 'my_table';", DEFAULT_KEYSPACE)).all();
    assertEquals(1, results.size());
    assertEquals(DEFAULT_KEYSPACE, results.get(0).getString("keyspace_name"));
    assertEquals("my_table", results.get(0).getString("table_name"));
    assertEquals(new HashSet<String>(Arrays.asList("compound")),
      results.get(0).getSet("flags", String.class));

    results = session.execute(
      "SELECT keyspace_name, table_name FROM system_schema.tables WHERE keyspace_name = " +
        "'my_keyspace' and table_name = 'my_table';").all();
    assertEquals(1, results.size());
    assertEquals("my_keyspace", results.get(0).getString("keyspace_name"));
    assertEquals("my_table", results.get(0).getString("table_name"));
    assertFalse(results.get(0).getColumnDefinitions().contains("flags")); // flags was not selected.
  }

  private void verifyColumnSchema(Row row, String table_name, String column_name, String kind,
                                  int position, String type, String clustering_order) {
    assertEquals(DEFAULT_KEYSPACE, row.getString("keyspace_name"));
    assertEquals(table_name, row.getString("table_name"));
    assertEquals(column_name, row.getString("column_name"));
    assertEquals(clustering_order, row.getString("clustering_order"));
    assertEquals(kind, row.getString("kind"));
    assertEquals(position, row.getInt("position"));
    assertEquals(type, row.getString("type"));
  }

  @Test
  public void testSystemColumnsTable() throws Exception {
    session.execute("CREATE TABLE many_columns (c1 int, c2 text, c3 int, c4 text, c5 int, c6 int," +
      " c7 map <text, text>, c8 list<text>, c9 set<int>, PRIMARY KEY((c1, c2, c3), c4, c5, c6)) " +
      "WITH CLUSTERING ORDER BY (c4 DESC);");
    List<Row> results = session.execute(String.format("SELECT * FROM system_schema.columns WHERE " +
      "keyspace_name = '%s' AND table_name = 'many_columns'", DEFAULT_KEYSPACE)).all();
    assertEquals(9, results.size());
    verifyColumnSchema(results.get(0), "many_columns", "c1", "partition_key", 0, "int", "none");
    verifyColumnSchema(results.get(1), "many_columns", "c2", "partition_key", 1, "text", "none");
    verifyColumnSchema(results.get(2), "many_columns", "c3", "partition_key", 2, "int", "none");
    verifyColumnSchema(results.get(3), "many_columns", "c4", "clustering", 0, "text", "desc");
    verifyColumnSchema(results.get(4), "many_columns", "c5", "clustering", 1, "int", "asc");
    verifyColumnSchema(results.get(5), "many_columns", "c6", "clustering", 2, "int", "asc");
    verifyColumnSchema(results.get(6), "many_columns", "c7", "regular", -1, "map<text, text>",
      "none");
    verifyColumnSchema(results.get(7), "many_columns", "c8", "regular", -1, "list<text>", "none");
    verifyColumnSchema(results.get(8), "many_columns", "c9", "regular", -1, "set<int>", "none");
  }
}
