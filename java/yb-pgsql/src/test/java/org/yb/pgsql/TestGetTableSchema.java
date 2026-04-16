package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes;
import org.yb.YBTestRunner;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass;

import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.fail;

@RunWith(value = YBTestRunner.class)
public class TestGetTableSchema extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestGetTableSchema.class);

  @Test
  public void testGetTableSchema() {
    LOG.info("Starting test testGetTableSchema");
    final String createTable = "CREATE TABLE test_table (id oid PRIMARY KEY, arr int[]);";
    final String createIndex = "CREATE INDEX ON test_table USING ybgin(arr);";
    try (Statement st = connection.createStatement()) {
      // Create table and index.
      st.execute(createTable);
      st.execute(createIndex);

      YBClient ybClient = miniCluster.getClient();

      // Get all the table UUIDs.
      ListTablesResponse resp = ybClient.getTablesList();
      Set<String> tableUUIDs = new HashSet<>();
      for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo tableInfo : resp.getTableInfoList()) {
        if (tableInfo.getTableType() == CommonTypes.TableType.PGSQL_TABLE_TYPE) {
          tableUUIDs.add(tableInfo.getId().toStringUtf8());
        }
      }

      // Now try get the table schema for all the tables. The API should not throw any exceptions.
      for (String tableId : tableUUIDs) {
        GetTableSchemaResponse response = ybClient.getTableSchemaByUUID(tableId);
        assertNotNull(response.getSchema());
      }
    } catch (Exception e) {
      fail("Test failed because of exception " + e);
    }
  }
}
