// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class DRLocalTest extends DRLocalTestBase {

  @Before
  public void setupNonDbDr() {
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        UniverseConfKeys.dbScopedXClusterCreationEnabled.getKey(),
        "false",
        true /* isSuperAdmin */);
  }

  @Test
  public void testDrConfigSetup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false /*disableTls*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);

    userIntent = getDefaultUserIntent("target-universe", false /*disableTLS*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig);

    // Get the table info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> resp =
        tableHandler.listNamespaces(customer.getUuid(), source.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "DisasterRecovery-1";
    formData.dbs = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespaceInfo : resp) {
      if (namespaceInfo.name.equals("yugabyte")) {
        formData.dbs.add(namespaceInfo.namespaceUUID.toString());
      }
    }
    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    assertYsqlOutputEqualsWithRetry(target, "select count(*) from some_table", "3");

    NodeDetails details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (4, 'dr1', 200), (5, 'dr2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    assertYsqlOutputEqualsWithRetry(target, "select count(*) from some_table", "5");
    verifyPayload();

    Result deleteResult = deleteDrConfig(UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(UUID.fromString(deleteJson.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    Thread.sleep(2000);

    details = source.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            source,
            YUGABYTE_DB,
            "insert into some_table values (6, 'dr3', 200), (7, 'dr4', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2000);
    details = target.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, target, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", CommonUtils.extractJsonisedSqlResponse(ysqlResponse).trim());
  }

  @Test
  public void testDRXClusterTableConfigStatus()
      throws InterruptedException, JsonMappingException, JsonProcessingException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("source-universe", false /*disableTls*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(source);
    initYCQL(source);

    userIntent = getDefaultUserIntent("target-universe", false /*disableTLS*/);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(target);
    initYCQL(target);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    Db db = Db.create("test_xcluster_db", false);
    Table table1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db);
    Table table2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db);
    Table table3 = Table.create("test_table_3", DEFAULT_TABLE_COLUMNS, db);
    Table table4 = Table.create("test_table_4", DEFAULT_TABLE_COLUMNS, db);
    createTestSet(source, Arrays.asList(db), Arrays.asList(table1, table2, table3));
    createTestSet(target, Arrays.asList(db), Arrays.asList(table1, table2, table3));

    // Get the table info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> resp =
        tableHandler.listNamespaces(customer.getUuid(), source.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = source.getUniverseUUID();
    formData.targetUniverseUUID = target.getUniverseUUID();
    formData.name = "DisasterRecovery-1";
    formData.dbs = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespaceInfo : resp) {
      if (namespaceInfo.name.equals("test_xcluster_db")) {
        formData.dbs.add(namespaceInfo.namespaceUUID.toString());
      }
    }
    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(source.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(target.getUniverseUUID()));
    UUID drConfigUuid = UUID.fromString(json.get("resourceUUID").asText());

    Result drConfigResult = getDrConfig(drConfigUuid);
    ObjectMapper mapper = new ObjectMapper();
    Set<XClusterTableConfig> tableDetails =
        mapper.readValue(
            Json.parse(contentAsString(drConfigResult)).get("tableDetails").toString(),
            new TypeReference<Set<XClusterTableConfig>>() {});
    assertEquals(3, tableDetails.size());
    for (XClusterTableConfig table : tableDetails) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    createTable(source, table4);
    createTable(target, table4);
    deleteTable(source, table1);
    deleteTable(target, table2);

    drConfigResult = getDrConfig(drConfigUuid);
    assertOk(drConfigResult);
    tableDetails =
        mapper.readValue(
            Json.parse(contentAsString(drConfigResult)).get("tableDetails").toString(),
            new TypeReference<Set<XClusterTableConfig>>() {});
    assertEquals(5, tableDetails.size());
    Set<XClusterTableConfig> runningTables =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.Running);
    assertEquals(1, runningTables.size());
    assertEquals("test_table_3", runningTables.iterator().next().getSourceTableInfo().tableName);
    Set<XClusterTableConfig> extraTablesOnSource =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.ExtraTableOnSource);
    assertEquals(2, extraTablesOnSource.size());
    for (XClusterTableConfig table : extraTablesOnSource) {
      assertTrue(
          table.getSourceTableInfo().tableName.equals("test_table_4")
              || table.getSourceTableInfo().tableName.equals("test_table_2"));
    }
    Set<XClusterTableConfig> extraTablesOnTarget =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.ExtraTableOnTarget);
    assertEquals(1, extraTablesOnTarget.size());
    assertEquals(
        "test_table_4", extraTablesOnTarget.iterator().next().getTargetTableInfo().tableName);
    Set<XClusterTableConfig> droppedTablesFromSource =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.DroppedFromSource);
    assertEquals(1, droppedTablesFromSource.size());
    assertEquals(
        "test_table_1", droppedTablesFromSource.iterator().next().getTargetTableInfo().tableName);
  }
}
