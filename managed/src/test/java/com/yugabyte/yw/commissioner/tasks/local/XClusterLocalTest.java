// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.XClusterTableConfig.Status;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class XClusterLocalTest extends XClusterLocalTestBase {

  @Test
  public void testXClusterConfigSetup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());

    // Get the table info for the source universe.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);

    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "Replication-1";
    formData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      formData.tables.add(tableInfo.tableID);
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    verifyYSQL(target);

    NodeDetails details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (4, 'xcluster1', 200), (5, 'xCluster2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2000);
    details = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, target, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", CommonUtils.extractJsonisedSqlResponse(ysqlResponse).trim());
    verifyPayload();
  }

  @Test
  public void testXClusterConfigAddTable() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());

    // Get the table info for the source universe.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);

    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "Replication-1";
    formData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      formData.tables.add(tableInfo.tableID);
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    verifyYSQL(target);

    NodeDetails sourceNodeDetails = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            "CREATE TABLE x_cluster (id int, name text, age int, PRIMARY KEY(id, name))",
            10);
    assertTrue(ysqlResponse.isSuccess());

    NodeDetails targetNodeDetails = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            targetNodeDetails,
            target,
            YUGABYTE_DB,
            "CREATE TABLE x_cluster (id int, name text, age int, PRIMARY KEY(id, name))",
            10);
    assertTrue(ysqlResponse.isSuccess());

    // Get the table info for the source universe.
    resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      editFormData.tables.add(tableInfo.tableID);
    }
    editFormData.bootstrapParams = formData.bootstrapParams;
    editFormData.bootstrapParams.tables = editFormData.tables;
    result = editXClusterConfig(editFormData, UUID.fromString(json.get("resourceUUID").asText()));
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            "insert into x_cluster values (4, 'xcluster1', 200), (5, 'xCluster2', 180)",
            10);

    assertTrue(ysqlResponse.isSuccess());
    Thread.sleep(2000);
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            targetNodeDetails, target, YUGABYTE_DB, "select count(*) from x_cluster", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("2", CommonUtils.extractJsonisedSqlResponse(ysqlResponse).trim());
    verifyPayload();
  }

  @Test
  public void testTableExistsOnTargetUniverseInBiDirectionalReplication()
      throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe sourceUniverse = createUniverseWithYbc(userIntent);
    initYSQL(sourceUniverse);
    initYCQL(sourceUniverse);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe targetUniverse = createUniverseWithYbc(userIntent);
    initYSQL(targetUniverse);
    initYCQL(targetUniverse);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());

    Db db = Db.create("test_xcluster_db", false);
    Table table1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db);
    Table table2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db);
    Table table3 = Table.create("test_table_3", DEFAULT_TABLE_COLUMNS, db);
    Table table4 = Table.create("test_table_4", DEFAULT_TABLE_COLUMNS, db);
    createTestSet(sourceUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));
    createTestSet(targetUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));

    // Get the table info for the source universe.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), sourceUniverse.getUniverseUUID(), false, false, true, true);
    // Create source to target xCluster config.
    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "bi-directional-xcluster-1";
    formData.tables = new HashSet<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db.name) && !tableInfo.tableName.equals(table4.name)) {
        formData.tables.add(tableInfo.tableID);
      }
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID xClusterConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    // Verify the table status on xCluster config.
    Result getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    XClusterConfig xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(3, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    // Create target to source xCluster config.
    resp =
        tableHandler.listTables(
            customer.getUuid(), targetUniverse.getUniverseUUID(), false, false, true, true);
    XClusterConfigCreateFormData formData2 = new XClusterConfigCreateFormData();
    formData2.sourceUniverseUUID = targetUniverse.getUniverseUUID();
    formData2.targetUniverseUUID = sourceUniverse.getUniverseUUID();
    formData2.name = "bi-directional-xcluster-2";
    formData2.tables = new HashSet<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db.name)) {
        formData2.tables.add(tableInfo.tableID);
      }
    }

    result = createXClusterConfig(formData2);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID xClusterConfigUuid2 = UUID.fromString(json.get("resourceUUID").asText());

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid2);
    assertOk(getXClusterConfigResult);
    xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);
    assertEquals(3, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    createTable(sourceUniverse, table4);
    String table4Id = "";
    // Get the table info for the source universe.
    resp =
        tableHandler.listTables(
            customer.getUuid(), sourceUniverse.getUniverseUUID(), false, false, true, true);
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db.name)) {
        editFormData.tables.add(tableInfo.tableID);
      }
      if (tableInfo.tableName.equals(table4.name)) {
        table4Id = tableInfo.tableID;
      }
    }
    editFormData.bootstrapParams = null;
    IllegalArgumentException exception =
        assertThrows(
            IllegalArgumentException.class,
            () -> editXClusterConfig(editFormData, xClusterConfigUuid));
    String errMsg =
        String.format(
            "Table ids [%s] do not have corresponding tables on the target universe and they must"
                + " be bootstrapped but bootstrapParams is null",
            table4Id);
    assertEquals(errMsg, exception.getMessage());
  }

  @Test
  public void testRemoveDBFromXClusterConfig() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe sourceUniverse = createUniverseWithYbc(userIntent);
    initYSQL(sourceUniverse);
    initYCQL(sourceUniverse);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe targetUniverse = createUniverseWithYbc(userIntent);
    initYSQL(targetUniverse);
    initYCQL(targetUniverse);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());

    Db db1 = Db.create("test_xcluster_db", false);
    Table table1Db1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db1);
    Table table2Db1 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db1);
    Db db2 = Db.create("test_xcluster_db_2", false);
    Table table1Db2 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db2);
    Table table2Db2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db2);

    createTestSet(
        sourceUniverse,
        Arrays.asList(db1, db2),
        Arrays.asList(table1Db1, table2Db1, table1Db2, table2Db2));
    createTestSet(
        targetUniverse,
        Arrays.asList(db1, db2),
        Arrays.asList(table1Db1, table2Db1, table1Db2, table2Db2));

    Set<String> db1TableIds = new HashSet<>();
    Set<String> db2TableIds = new HashSet<>();
    // Get the table info for the source universe and create xCluster config.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), sourceUniverse.getUniverseUUID(), false, false, true, true);
    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "xCluster-config-with-different-table-status";
    formData.tables = new HashSet<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (Arrays.asList(db1.name, db2.name).contains(tableInfo.keySpace)) {
        if (tableInfo.keySpace.equals(db1.name)) {
          db1TableIds.add(tableInfo.tableID);
        } else if (tableInfo.keySpace.equals(db2.name)) {
          db2TableIds.add(tableInfo.tableID);
        }
        formData.tables.add(tableInfo.tableID);
      }
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID xClusterConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    // Verify the table status on xCluster config.
    Result getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    XClusterConfig xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(4, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    // Set the status of db2 tables to Failed and unset replicationSetupDone.
    xClusterConfig = XClusterConfig.getOrBadRequest(xClusterConfigUuid);
    Set<XClusterTableConfig> existingTableConfigs = xClusterConfig.getTableDetails();
    for (XClusterTableConfig table : existingTableConfigs) {
      if (db2TableIds.contains(table.getTableId())) {
        table.setStatus(XClusterTableConfig.Status.Failed);
        table.setReplicationSetupDone(false);
        table.update();
      }
    }

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);
    assertEquals(4, xClusterConfig.getTableIds().size());
    Set<XClusterTableConfig> runningTables =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.Running);
    assertEquals(2, runningTables.size());
    Set<XClusterTableConfig> failedTables =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.Failed);
    assertEquals(2, failedTables.size());

    // Allow user to drop failed DB from xCluster config.
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = new HashSet<String>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db1.name)) {
        editFormData.tables.add(tableInfo.tableID);
      }
    }
    editFormData.bootstrapParams = formData.bootstrapParams;
    editFormData.bootstrapParams.tables = editFormData.tables;
    result = editXClusterConfig(editFormData, xClusterConfigUuid);
    json = Json.parse(contentAsString(result));
    taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);
    assertEquals(2, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    editFormData.tables = new HashSet<String>();
    // Set status to Paused so that we can make the controller believe that an
    // edit operation is performed.
    editFormData.status = "Paused";
    result = assertPlatformException(() -> editXClusterConfig(editFormData, xClusterConfigUuid));
    assertBadRequest(
        result,
        "The operation to remove tables from replication config will remove all the "
            + "tables in replication which is not allowed; if you want to delete "
            + "replication for all of them, please delete the replication config");
  }

  @Test
  @Parameters({"Basic", "Txn"})
  public void testYSQLXClusterConfigTablesStatus(String configType) throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe sourceUniverse = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(sourceUniverse);
    initYCQL(sourceUniverse);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe targetUniverse = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(targetUniverse);
    initYCQL(targetUniverse);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    Db db = Db.create("test_xcluster_db", false);
    Table table1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db);
    Table table2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db);
    Table table3 = Table.create("test_table_3", DEFAULT_TABLE_COLUMNS, db);
    Table table4 = Table.create("test_table_4", DEFAULT_TABLE_COLUMNS, db);
    createTestSet(sourceUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));
    createTestSet(targetUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));

    // Get the table info for the source universe and create xCluster config.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), sourceUniverse.getUniverseUUID(), false, false, true, true);
    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "xCluster-config-with-different-table-status";
    formData.configType = XClusterConfig.ConfigType.getFromString(configType);
    formData.tables = new HashSet<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db.name)) {
        formData.tables.add(tableInfo.tableID);
      }
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID xClusterConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    // Verify the table status on xCluster config.
    Result getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    XClusterConfig xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(3, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }
    createTable(sourceUniverse, table4);
    createTable(targetUniverse, table4);
    deleteTable(sourceUniverse, table1);
    deleteTable(targetUniverse, table2);

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(5, xClusterConfig.getTableDetails().size());
    Set<XClusterTableConfig> runningTables =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.Running);
    assertEquals(1, runningTables.size());
    assertEquals("test_table_3", runningTables.iterator().next().getSourceTableInfo().tableName);
    Set<XClusterTableConfig> extraTablesOnSource =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.ExtraTableOnSource);
    assertEquals(2, extraTablesOnSource.size());
    for (XClusterTableConfig table : extraTablesOnSource) {
      assertTrue(
          table.getSourceTableInfo().tableName.equals("test_table_4")
              || table.getSourceTableInfo().tableName.equals("test_table_2"));
    }
    Set<XClusterTableConfig> extraTablesOnTarget =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.ExtraTableOnTarget);
    assertEquals(1, extraTablesOnTarget.size());
    assertEquals(
        "test_table_4", extraTablesOnTarget.iterator().next().getTargetTableInfo().tableName);
    Set<XClusterTableConfig> droppedTablesFromSource =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.DroppedFromSource);
    assertEquals(1, droppedTablesFromSource.size());
    assertEquals(
        "test_table_1", droppedTablesFromSource.iterator().next().getTargetTableInfo().tableName);
  }

  @Test
  public void testYCQLXClusterConfigTablesStatus() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe sourceUniverse = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(sourceUniverse);
    initYCQL(sourceUniverse);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe targetUniverse = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(targetUniverse);
    initYCQL(targetUniverse);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    Db db = Db.create("test_xcluster_db", false, TableType.YCQL);
    Table table1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db);
    Table table2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db);
    IndexTable indexTable1 = IndexTable.create("test_index_table", "id", db, table1);
    IndexTable indexTable2 = IndexTable.create("test_index_table_2", "id", db, table2);
    IndexTable indexTable3 = IndexTable.create("test_index_table_3", "id", db, table2);
    IndexTable indexTable4 = IndexTable.create("test_index_table_4", "id", db, table2);

    List<Table> tables = Arrays.asList(table1, table2);
    createTestSet(sourceUniverse, Arrays.asList(db), tables);
    createIndexTable(sourceUniverse, indexTable1);
    createIndexTable(sourceUniverse, indexTable2);
    createTestSet(targetUniverse, Arrays.asList(db), tables);
    createIndexTable(targetUniverse, indexTable1);
    createIndexTable(targetUniverse, indexTable2);

    // Get the table info for the source universe and create xCluster config.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), sourceUniverse.getUniverseUUID(), false, false, true, true);
    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "xCluster-config-with-different-table-status";
    formData.configType = XClusterConfig.ConfigType.Basic;
    formData.tables = new HashSet<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.keySpace.equals(db.name)) {
        formData.tables.add(tableInfo.tableID);
      }
    }
    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID xClusterConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    // Verify the table status on xCluster config.
    Result getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    XClusterConfig xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(4, xClusterConfig.getTableIds().size());
    for (XClusterTableConfig table : xClusterConfig.getTableDetails()) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    createIndexTable(sourceUniverse, indexTable3);
    createIndexTable(targetUniverse, indexTable4);

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfig =
        Json.fromJson(Json.parse(contentAsString(getXClusterConfigResult)), XClusterConfig.class);

    assertEquals(6, xClusterConfig.getTableDetails().size());
    Set<XClusterTableConfig> runningTables =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.Running);
    List<String> expectedRunningStatusTables =
        Arrays.asList("test_table_1", "test_table_2", "test_index_table", "test_index_table_2");
    assertEquals(4, runningTables.size());
    for (XClusterTableConfig table : runningTables) {
      assertTrue(expectedRunningStatusTables.contains(table.getSourceTableInfo().tableName));
    }

    Set<XClusterTableConfig> extraTablesOnSource =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.ExtraTableOnSource);
    assertEquals(1, extraTablesOnSource.size());
    assertEquals(
        "test_index_table_3", extraTablesOnSource.iterator().next().getSourceTableInfo().tableName);
    Set<XClusterTableConfig> extraTablesOnTarget =
        xClusterConfig.getTableDetailsWithStatus(XClusterTableConfig.Status.ExtraTableOnTarget);
    assertEquals(1, extraTablesOnTarget.size());
    assertEquals(
        "test_index_table_4", extraTablesOnTarget.iterator().next().getTargetTableInfo().tableName);
  }

  @Test
  public void testXClusterConfigHandleDroppedAndReAddedTable() throws InterruptedException {
    // Step 1: Create a new xCluster replication between source and target universes.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());

    // Get the table info for the source universe and create xCluster config.
    List<TableInfoForm.TableInfoResp> resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);
    XClusterConfigCreateFormData formData = new XClusterConfigCreateFormData();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "Replication-Handle-Dropped-Table";
    formData.tables = new HashSet<>();
    Map<String, String> tableIdToName = new HashMap<>();
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      formData.tables.add(tableInfo.tableID);
      tableIdToName.put(tableInfo.tableID, tableInfo.tableName);
    }
    log.debug("tableIdToName is {}", tableIdToName);

    formData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    formData.bootstrapParams.tables = formData.tables;
    formData.bootstrapParams.backupRequestParams = new BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createXClusterConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    UUID xClusterConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    // Step 2: Drop a table from the source universe and validate the status change.
    NodeDetails sourceNodeDetails = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    String tableToDropId = formData.tables.iterator().next(); // Pick the first table to drop.
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            String.format("DROP TABLE %s", tableIdToName.get(tableToDropId)),
            10);
    assertTrue(ysqlResponse.isSuccess());

    // Verify that the table shows as "Dropped From Source" in the xCluster configuration.
    Result getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    JsonNode xClusterConfigJson = Json.parse(contentAsString(getXClusterConfigResult));
    XClusterConfig xClusterConfig = Json.fromJson(xClusterConfigJson, XClusterConfig.class);

    assertTrue(xClusterConfig.getTableIds().contains(tableToDropId));
    assertEquals(Status.DroppedFromSource, xClusterConfig.getTableById(tableToDropId).getStatus());

    // Step 3: Re-add the table to the source universe (it will have a new table ID).
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            sourceNodeDetails,
            source,
            YUGABYTE_DB,
            String.format(
                "CREATE TABLE %s (id int, name text, age int, PRIMARY KEY(id, name))",
                tableIdToName.get(tableToDropId)),
            10);
    assertTrue(ysqlResponse.isSuccess());
    log.debug(
        "tableToDropId: {}, tableToDropName: {}", tableToDropId, tableIdToName.get(tableToDropId));

    // Fetch the new table information.
    resp =
        tableHandler.listTables(
            customer.getUuid(), source.getUniverseUUID(), false, false, true, true);
    String newTableID = null;
    for (TableInfoForm.TableInfoResp tableInfo : resp) {
      if (tableInfo.tableName.equals(tableIdToName.get(tableToDropId))
          && !formData.tables.contains(tableInfo.tableID)) {
        newTableID = tableInfo.tableID;
        tableIdToName.put(newTableID, tableInfo.tableName);
        break;
      }
    }
    assertNotNull("New table ID should be found", newTableID);

    // Verify that the re-added table shows as "Extra Table on Source".
    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfigJson = Json.parse(contentAsString(getXClusterConfigResult));
    xClusterConfig = Json.fromJson(xClusterConfigJson, XClusterConfig.class);
    assertTrue(xClusterConfig.getTableIds().contains(newTableID));
    assertEquals(Status.ExtraTableOnSource, xClusterConfig.getTableById(newTableID).getStatus());

    xClusterConfig
        .getTables()
        .forEach(
            table -> {
              log.debug(
                  "Tables in the xCluster config: {} {}", table.getTableId(), table.getStatus());
            });

    // Step 4: Remove the dropped table from the existing replication.
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables =
        formData.tables.stream().filter(t -> !t.equals(tableToDropId)).collect(Collectors.toSet());
    editFormData.bootstrapParams = formData.bootstrapParams;
    editFormData.bootstrapParams.tables = editFormData.tables;
    result = editXClusterConfig(editFormData, xClusterConfig.getUuid());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfigJson = Json.parse(contentAsString(getXClusterConfigResult));
    xClusterConfig = Json.fromJson(xClusterConfigJson, XClusterConfig.class);
    xClusterConfig
        .getTables()
        .forEach(
            table -> {
              log.debug(
                  "Tables in the xCluster config: {} {}", table.getTableId(), table.getStatus());
            });

    // Step 5: Add the re-added table to the existing replication.
    XClusterConfigEditFormData editFormData2 = new XClusterConfigEditFormData();
    editFormData2.tables = new HashSet<>(editFormData.tables);
    editFormData2.tables.add(newTableID);
    editFormData2.bootstrapParams = formData.bootstrapParams;
    editFormData2.bootstrapParams.tables = editFormData2.tables;
    result = editXClusterConfig(editFormData2, UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    // Step 6: Validate the final status is "Running".
    getXClusterConfigResult = getXClusterConfig(xClusterConfigUuid);
    assertOk(getXClusterConfigResult);
    xClusterConfigJson = Json.parse(contentAsString(getXClusterConfigResult));
    xClusterConfig = Json.fromJson(xClusterConfigJson, XClusterConfig.class);
    assertEquals(Status.Running, xClusterConfig.getTableById(newTableID).getStatus());

    // Clean up and verify universe states.
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    log.info("Test completed successfully.");
  }
}
