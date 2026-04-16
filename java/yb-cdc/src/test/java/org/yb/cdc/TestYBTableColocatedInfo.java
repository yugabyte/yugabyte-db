package org.yb.cdc;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;

import static org.yb.AssertionWrappers.*;

import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

@RunWith(value = YBTestRunner.class)
public class TestYBTableColocatedInfo extends CDCBaseClass {
  private final Logger LOGGER = LoggerFactory.getLogger(TestYBTableColocatedInfo.class);

  private CDCSubscriber testSubscriber;

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
  }

  @Test
  public void testIfColocationMetadataPresent() throws Exception {
    final String COLOCATED_DB = "colocated_db";
    statement.execute("drop database if exists " + COLOCATED_DB + ";");
    statement.execute("create database " + COLOCATED_DB + " with colocated = true;");

    final InetSocketAddress pgAddress = miniCluster.getPostgresContactPoints().get(0);
    String url = String.format("jdbc:yugabytedb://%s:%d/%s", pgAddress.getHostName(),
      pgAddress.getPort(), COLOCATED_DB);
    Properties props = new Properties();
    props.setProperty("user", DEFAULT_PG_USER);

    try (Connection conn = DriverManager.getConnection(url, props)) {
      Statement st = conn.createStatement();
      st.execute("CREATE TABLE test_1 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      st.execute("CREATE TABLE test_2 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");
      st.execute("CREATE TABLE test_3 (id INT PRIMARY KEY, name TEXT) WITH (COLOCATED = true);");

      // Close statement.
      st.close();
    }

    testSubscriber = new CDCSubscriber(COLOCATED_DB, "test_1", getMasterAddresses());
    testSubscriber.createStream("proto");
    String dbStreamId = testSubscriber.getDbStreamId();

    List<String> tableIds = new ArrayList<>();
    YBClient ybClient = testSubscriber.getSyncClient();

    ListTablesResponse resp = ybClient.getTablesList();
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo : resp.getTableInfoList()) {
      if (tableInfo.getName().equals("test_1")
            || tableInfo.getName().equals("test_2")
            || tableInfo.getName().equals("test_3")) {
        tableIds.add(tableInfo.getId().toStringUtf8());
      }
    }

    // Open tables and check if it is colocated.
    for (String tableId : tableIds) {
      YBTable table = ybClient.openTableByUUID(tableId);
      assertTrue(table.isColocated());
    }

    // Cleanup the tables created in the colocated database for this test.
    try (Connection conn = DriverManager.getConnection(url, props)) {
      Statement st = conn.createStatement();

      st.execute("DROP TABLE IF EXISTS test_1;");
      st.execute("DROP TABLE IF EXISTS test_2;");
      st.execute("DROP TABLE IF EXISTS test_3;");

      // Close statement and connection
      st.close();
    }

    // Drop the colocated database created as a part of this test.
    statement.execute("DROP DATABASE " + COLOCATED_DB + ";");
  }
}
