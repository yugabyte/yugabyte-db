package org.yb.cql;

import java.net.InetAddress;
import java.util.List;

import org.junit.Test;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

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
    assertEquals(0, session.execute("SELECT * FROM system_schema.columns;").all().size());
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
      String.format("SELECT keyspace_name, table_name FROM system_schema.tables WHERE " +
        "keyspace_name = " +
        "'%s' and table_name = 'my_table';", DEFAULT_KEYSPACE)).all();
    assertEquals(1, results.size());
    assertEquals(DEFAULT_KEYSPACE, results.get(0).getString("keyspace_name"));
    assertEquals("my_table", results.get(0).getString("table_name"));

    results = session.execute(
      "SELECT keyspace_name, table_name FROM system_schema.tables WHERE keyspace_name = " +
        "'my_keyspace' and table_name = 'my_table';").all();
    assertEquals(1, results.size());
    assertEquals("my_keyspace", results.get(0).getString("keyspace_name"));
    assertEquals("my_table", results.get(0).getString("table_name"));
  }
}
