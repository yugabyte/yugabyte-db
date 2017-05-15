package org.yb.cql;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;

import org.junit.Test;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.yb.minicluster.MiniYBCluster;
import org.yb.master.Master;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertNotNull;

public class TestSystemTables extends BaseCQLTest {

  private void verifyPeersTable(List<Row> rows) throws Exception {
    List<InetSocketAddress> contactPoints = miniCluster.getCQLContactPoints();
    // One of the contact points will be missing, since that is the node we connected to.
    assertEquals(contactPoints.size() - 1, rows.size());
    for (Row row : rows) {
      boolean found = false;
      for (InetSocketAddress addr : contactPoints) {
        if (addr.getAddress().equals(row.getInet("peer")) &&
            addr.getAddress().equals(row.getInet("rpc_address"))) {
          found = true;
        }
      }
      assertNotNull(row.getUUID("host_id"));
      assertNotNull(row.getString("data_center"));
      assertNotNull(row.getString("rack"));
      assertTrue(found);
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
    List<InetSocketAddress> contactPoints = miniCluster.getCQLContactPoints();
    assertEquals(NUM_TABLET_SERVERS, contactPoints.size());

    ResultSet rs = session.execute("SELECT * FROM system.peers;");
    List <Row> rows = rs.all();
    verifyPeersTable(rows);

    rs = session.execute("SELECT * FROM system.peers WHERE peer = '181.123.12.1'");
    assertEquals(0, rs.all().size());

    // Now kill a tablet server and verify peers table has one less entry.
    miniCluster.killTabletServerOnHostPort(
        miniCluster.getTabletServers().keySet().iterator().next());

    // Wait for TServer to timeout.
    Thread.sleep(2 * MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS);

    // Now verify one tserver is missing.
    long start = System.currentTimeMillis();
    while (System.currentTimeMillis() - start < 30000) {
      try {
        rs = session.execute("SELECT * FROM system.peers;");
        break;
      } catch (Exception e) {
        // Ignoring exception since reconnection might be in progress.
        Thread.sleep(1000);
      }
    }

    // -2 since the node we are connected to won't be in the peers table and we killed one node
    // above.
    assertEquals(NUM_TABLET_SERVERS - 2, rs.all().size());

    // Start the tserver back up and wait for NUM_TABLET_SERVERS + 1 (since master never forgets
    // tservers).
    miniCluster.startTServer(null);
    assertTrue(miniCluster.waitForTabletServers(NUM_TABLET_SERVERS + 1));
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

  private void checkContactPoints(String column, Row row) {
    List<InetSocketAddress> contactPoints = miniCluster.getCQLContactPoints();
    boolean found = false;
    for (InetSocketAddress addr : contactPoints) {
      if (addr.getAddress().equals(row.getInet(column))) {
        found = true;
      }
    }
    assertTrue(found);
  }

  @Test
  public void testSystemLocalTables() throws Exception {
    List <Row> results = session.execute(
      "SELECT * FROM system.local;").all();
    assertEquals(1, results.size());
    Row row = results.get(0);
    assertEquals("local", row.getString("key"));
    assertEquals("COMPLETED", row.getString("bootstrapped"));
    checkContactPoints("broadcast_address", row);
    assertEquals("local cluster", row.getString("cluster_name"));
    assertEquals("3.4.2", row.getString("cql_version"));
    assertEquals("", row.getString("data_center"));
    assertEquals(0, row.getInt("gossip_generation"));
    checkContactPoints("listen_address", row);
    assertEquals("4", row.getString("native_protocol_version"));
    assertEquals("org.apache.cassandra.dht.Murmur3Partitioner", row.getString("partitioner"));
    assertEquals("rack", row.getString("rack"));
    assertEquals("3.9-SNAPSHOT", row.getString("release_version"));
    checkContactPoints("rpc_address", row);
    assertEquals("20.1.0", row.getString("thrift_version"));
    assertTrue(row.isNull("tokens"));

    // Verify where clauses and projections work.
    results = session.execute("SELECT tokens, partitioner, key FROM system.local;").all();
    assertEquals(1, results.size());
    row = results.get(0);
    assertEquals("local", row.getString("key"));
    assertEquals("org.apache.cassandra.dht.Murmur3Partitioner", row.getString("partitioner"));
    assertTrue(row.isNull("tokens"));
    assertFalse(row.getColumnDefinitions().contains("cluster_name"));
    assertFalse(row.getColumnDefinitions().contains("bootstrapped"));
    assertFalse(row.getColumnDefinitions().contains("broadcast_address"));
    assertFalse(row.getColumnDefinitions().contains("cql_version"));
    assertFalse(row.getColumnDefinitions().contains("data_center"));
    assertFalse(row.getColumnDefinitions().contains("gossip_generation"));
    assertFalse(row.getColumnDefinitions().contains("host_id"));
    assertFalse(row.getColumnDefinitions().contains("listen_address"));
    assertFalse(row.getColumnDefinitions().contains("native_protocol_version"));
    assertFalse(row.getColumnDefinitions().contains("rack"));
    assertFalse(row.getColumnDefinitions().contains("release_version"));
    assertFalse(row.getColumnDefinitions().contains("rpc_address"));
    assertFalse(row.getColumnDefinitions().contains("schema_version"));
    assertFalse(row.getColumnDefinitions().contains("thrift_version"));
    assertFalse(row.getColumnDefinitions().contains("truncated_at"));

    results = session.execute("SELECT * FROM system.local WHERE key = 'randomkey';").all();
    assertEquals(0, results.size());

    results = session.execute("SELECT partitioner FROM system.local WHERE key = 'local';").all();
    assertEquals(1, results.size());
    row = results.get(0);
    assertEquals("org.apache.cassandra.dht.Murmur3Partitioner", row.getString("partitioner"));
  }

  @Test
  public void testSystemKeyspacesAndTables() throws Exception {
    List <Row> results = session.execute(
      "SELECT * FROM system_schema.keyspaces;").all();
    assertEquals(4, results.size());
    assertEquals(DEFAULT_TEST_KEYSPACE, results.get(0).getString("keyspace_name"));
    assertTrue(results.get(0).getBool("durable_writes"));
    assertEquals("system_schema", results.get(1).getString("keyspace_name"));
    assertTrue(results.get(1).getBool("durable_writes"));
    assertEquals("system", results.get(2).getString("keyspace_name"));
    assertTrue(results.get(2).getBool("durable_writes"));
    assertEquals(DEFAULT_KEYSPACE, results.get(3).getString("keyspace_name"));
    assertTrue(results.get(3).getBool("durable_writes"));

    results = session.execute(
      "SELECT * FROM system_schema.tables;").all();
    assertEquals(11, results.size());
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
    verifySystemSchemaTables(results, "system", "local");

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
        "'%s' and table_name = 'my_table';", DEFAULT_TEST_KEYSPACE)).all();
    assertEquals(1, results.size());
    assertEquals(DEFAULT_TEST_KEYSPACE, results.get(0).getString("keyspace_name"));
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

    // Verify table id.
    results = session.execute("SELECT id FROM system_schema.tables WHERE keyspace_name = " +
      "'my_keyspace' and table_name = 'my_table'").all();
    assertEquals(1, results.size());
    byte[] table_uuid = null;
    for (Master.ListTablesResponsePB.TableInfo tableInfo :
      miniCluster.getClient().getTablesList("my_table").getTableInfoList()) {
      if (tableInfo.getNamespace().getName().equals("my_keyspace") &&
        tableInfo.getName().equals("my_table")) {
        table_uuid = tableInfo.getId().toByteArray();
      }
    }

    // Flip two adjacent chars (so that we reverse bytes correctly below).
    for (int i = 0; i + 1 < table_uuid.length; i+=2) {
      byte tmp = table_uuid[i];
      table_uuid[i] = table_uuid[i + 1];
      table_uuid[i + 1] = tmp;
    }

    assertNotNull(table_uuid);
    assertEquals(32, table_uuid.length);
    // Reverse bytes since we have UUID in host byte order in TableInfo, but network byte order
    // in the system table.
    String uuid = new StringBuilder(new String(table_uuid)).reverse().toString();

    // Insert hyphens.
    uuid = String.format("%s-%s-%s-%s-%s", uuid.substring(0, 8), uuid.substring
      (8, 12), uuid.substring(12, 16), uuid.substring(16, 20), uuid.substring
      (20, 32));
    assertEquals(UUID.fromString(uuid), results.get(0).getUUID("id"));
  }

  private void verifyColumnSchema(Row row, String table_name, String column_name, String kind,
                                  int position, String type, String clustering_order) {
    assertEquals(DEFAULT_TEST_KEYSPACE, row.getString("keyspace_name"));
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
      "keyspace_name = '%s' AND table_name = 'many_columns'", DEFAULT_TEST_KEYSPACE)).all();
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

  @Test
  public void testSchemaUpdate() throws Exception {
    // Create keyspace and table.
    session.execute("CREATE KEYSPACE test_keyspace;");
    session.execute("USE test_keyspace;");
    session.execute("CREATE TABLE test_table (k int primary key, c int);");

    // Look up and verify the keyspace and table metadata by the CQL statement generated.
    String cql = cluster.getMetadata()
                        .getKeyspace("test_keyspace")
                        .getTable("test_table")
                        .exportAsString();
    assertTrue("CQL of test_table mismatch: " + cql,
               cql.startsWith("CREATE TABLE test_keyspace.test_table (\n"+
                              "    k int,\n" +
                              "    c int,\n" +
                              "    PRIMARY KEY (k)\n"+
                              ")"));

    // Drop the table. Verify its metadata has been removed.
    session.execute("DROP TABLE test_table;");
    assertNull(cluster.getMetadata().getKeyspace("test_keyspace").getTable("test_table"));

    // Drop the keyspace. Verify its metadata has been removed.
    session.execute("DROP KEYSPACE test_keyspace;");
    assertNull(cluster.getMetadata().getKeyspace("test_keyspace"));
  }

}
