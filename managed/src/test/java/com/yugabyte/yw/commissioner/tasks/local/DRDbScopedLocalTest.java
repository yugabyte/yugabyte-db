// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class DRDbScopedLocalTest extends DRLocalTestBase {

  public static final String DB_SCOPED_MIN_VERSION = "2.23.0.0-b394";
  public static String DB_SCOPE_MIN_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.23.0.0-b394/yugabyte-2.23.0.0-b394-%s-%s.tar.gz";

  @Before
  public void setupDrDbScoped() {
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.dbScopedXClusterEnabled.getKey(),
        "true",
        true);

    downloadAndSetUpYBSoftware(
        os, arch, String.format(DB_SCOPE_MIN_VERSION_URL, os, arch), DB_SCOPED_MIN_VERSION);
    ybVersion = DB_SCOPED_MIN_VERSION;
    ybBinPath = deriveYBBinPath(ybVersion);
    log.debug("YB version {} bin path new {}", ybVersion, ybBinPath);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(
        DB_SCOPED_MIN_VERSION,
        getMetadataJson(DB_SCOPED_MIN_VERSION, false).get(DB_SCOPED_MIN_VERSION));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  public Universe createDRUniverse(String DBVersion, String universeName, boolean disableTls)
      throws InterruptedException {
    return createDRUniverse(DBVersion, universeName, disableTls, 3, 3);
  }

  public Universe createDRUniverse(
      String DBVersion,
      String universeName,
      boolean disableTls,
      int numNodes,
      int replicationFactor)
      throws InterruptedException {
    ybVersion = DBVersion;
    ybBinPath = deriveYBBinPath(DBVersion);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent(universeName, disableTls);
    userIntent.numNodes = numNodes;
    userIntent.replicationFactor = replicationFactor;

    // Set to use new db version for master/tserver.
    Provider provider = Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    LocalCloudInfo localCloudInfo = provider.getDetails().getCloudInfo().getLocal();
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    provider.update();

    userIntent.ybcFlags = getYbcGFlags(userIntent);
    return createUniverseWithYbc(userIntent);
  }

  @Test
  public void testDrDbScopedSetupNoBootstrap() throws InterruptedException {
    Universe sourceUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "source-universe", true);
    Universe targetUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "target-universe", true);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    List<String> namespaceNames = Arrays.asList("dbnoncolocated", "dbcolocated");
    Db db1 = Db.create(namespaceNames.get(0), false);
    Db db2 = Db.create(namespaceNames.get(1), true);
    List<Db> dbs = Arrays.asList(db1, db2);

    Table table1 = Table.create("table1", DEFAULT_TABLE_COLUMNS, db1);
    Table table2 = Table.create("table2", DEFAULT_TABLE_COLUMNS, db2);
    Table table3 = Table.create("table3", DEFAULT_TABLE_COLUMNS, db2, true /* escapeColocation */);
    List<Table> tables = Arrays.asList(table1, table2, table3);

    // Create databases on both source + target universe.
    createTestSet(sourceUniverse, dbs, tables);
    createTestSet(targetUniverse, dbs, tables);

    // Get the namespace info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), sourceUniverse.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "db-scoped-disaster-recovery-1";
    formData.dbScoped = true;
    formData.dbs = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (namespaceNames.contains(namespace.name)) {
        formData.dbs.add(namespace.namespaceUUID.toString());
      }
    }

    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    Thread.sleep(5000);
    int rowCount = getRowCount(targetUniverse, table1);
    assertEquals(1, rowCount);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table2);
    assertEquals(1, rowCount);

    // Delete db scoped DR.
    Result deleteResult = deleteDrConfig(UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(
            UUID.fromString(deleteJson.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    Thread.sleep(5000);

    // Inserting values should not be replicated since replication is deleted.
    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table1);
    assertEquals(1, rowCount);
  }

  @Test
  public void testDrDbScopedSetupWithBootstrap() throws InterruptedException {
    Universe sourceUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "source-universe", true);
    Universe targetUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "target-universe", true);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    List<String> namespaceNames =
        Arrays.asList("dbnoncolocated1", "dbcolocated", "dbnoncolocated2");
    Db db1 = Db.create(namespaceNames.get(0), false);
    Db db2 = Db.create(namespaceNames.get(1), true);
    Db db3 = Db.create(namespaceNames.get(2), false);
    List<Db> dbs = Arrays.asList(db1, db2, db3);

    Table table1 = Table.create("table1", DEFAULT_TABLE_COLUMNS, db1);
    Table table2 = Table.create("table2", DEFAULT_TABLE_COLUMNS, db2);
    Table table3 = Table.create("table3", DEFAULT_TABLE_COLUMNS, db2, true /* escapeColocation */);
    Table table4 = Table.create("table4", DEFAULT_TABLE_COLUMNS, db3);
    List<Table> tables = Arrays.asList(table1, table2, table3, table4);

    // Create databases on both source + target universe.
    createTestSet(sourceUniverse, dbs, tables);
    createTestSet(targetUniverse, dbs, tables);

    // Get the namespace info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), sourceUniverse.getUniverseUUID(), false);

    // Insert values into source universe to make backup/restore needed
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    insertRow(sourceUniverse, table4, Map.of("id", "2", "name", "'val2'"));
    Thread.sleep(2000);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "db-scoped-disaster-recovery-1";
    formData.dbScoped = true;
    formData.dbs = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (namespaceNames.contains(namespace.name)) {
        formData.dbs.add(namespace.namespaceUUID.toString());
      }
    }
    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    // Check backup/restore objects are created.
    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), sourceUniverse.getUniverseUUID());
    assertEquals(2, backups.size());

    List<Restore> restores =
        Restore.fetchByUniverseUUID(customer.getUuid(), targetUniverse.getUniverseUUID());
    assertEquals(2, restores.size());

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "3", "name", "'val3'"));
    Thread.sleep(5000);
    int rowCount = getRowCount(targetUniverse, table1);
    log.debug("row count {}", rowCount);
    assertEquals(2, rowCount);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table2);
    assertEquals(1, rowCount);

    // Delete db scoped DR.
    Result deleteResult = deleteDrConfig(UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(
            UUID.fromString(deleteJson.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
  }

  @Test
  public void testDrDbScopedUpdate() throws InterruptedException {
    Universe sourceUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "source-universe", true);
    Universe targetUniverse = createDRUniverse(DB_SCOPED_MIN_VERSION, "target-universe", true);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    List<String> namespaceNames = Arrays.asList("dbnoncolocated", "dbcolocated");
    Db db1 = Db.create(namespaceNames.get(0), false);
    Db db2 = Db.create(namespaceNames.get(1), true);
    List<Db> dbs = Arrays.asList(db1, db2);

    Table table1 = Table.create("table1", DEFAULT_TABLE_COLUMNS, db1);
    Table table2 = Table.create("table2", DEFAULT_TABLE_COLUMNS, db2);
    Table table3 = Table.create("table3", DEFAULT_TABLE_COLUMNS, db2, true /* escapeColocation */);
    List<Table> tables = Arrays.asList(table1, table2, table3);

    // Create databases on both source + target universe.
    createTestSet(sourceUniverse, dbs, tables);
    createTestSet(targetUniverse, dbs, tables);

    // Get the namespace info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), sourceUniverse.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "db-scoped-disaster-recovery-1";
    formData.dbScoped = true;
    formData.dbs = new HashSet<String>();
    List<String> createNamespaceNames = Arrays.asList("dbnoncolocated");
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (createNamespaceNames.contains(namespace.name)) {
        formData.dbs.add(namespace.namespaceUUID.toString());
      }
    }

    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    UUID drConfigUUID = UUID.fromString(json.get("resourceUUID").asText());

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    Thread.sleep(5000);
    int rowCount = getRowCount(targetUniverse, table1);
    assertEquals(1, rowCount);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table2);
    assertEquals(0, rowCount);

    List<String> updateNamespaceNames = Arrays.asList("dbcolocated");
    DrConfigSetDatabasesForm setDatabasesFormData = new DrConfigSetDatabasesForm();
    setDatabasesFormData.databases = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (updateNamespaceNames.contains(namespace.name)) {
        setDatabasesFormData.databases.add(namespace.namespaceUUID.toString());
      }
    }

    // Only the database 'dbcolocated' will be under replication in the final state.
    result = setDatabasesDrConfig(drConfigUUID, setDatabasesFormData);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    Thread.sleep(5000);
    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table1);
    assertEquals(1, rowCount);

    insertRow(sourceUniverse, table2, Map.of("id", "12", "name", "'val12'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table2);
    assertEquals(2, rowCount);

    // Delete db scoped DR.
    Result deleteResult = deleteDrConfig(drConfigUUID);
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(
            UUID.fromString(deleteJson.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
  }

  @Test
  public void testDrDbScopedWithTLS() throws InterruptedException {
    Universe sourceUniverse =
        createDRUniverse(
            DB_SCOPED_MIN_VERSION,
            "source-universe",
            false,
            1 /* numNodes */,
            1 /* replicationFactor */);
    Universe targetUniverse =
        createDRUniverse(
            DB_SCOPED_MIN_VERSION,
            "target-universe",
            false,
            1 /* numNodes */,
            1 /* replicationFactor */);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    List<String> namespaceNames = Arrays.asList("dbnoncolocated");
    Db db1 = Db.create(namespaceNames.get(0), false);
    List<Db> dbs = Arrays.asList(db1);

    Table table1 = Table.create("table1", DEFAULT_TABLE_COLUMNS, db1);
    List<Table> tables = Arrays.asList(table1);

    // Create databases on both source + target universe.
    createTestSet(sourceUniverse, dbs, tables);
    createTestSet(targetUniverse, dbs, tables);

    // Get the namespace info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), sourceUniverse.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "db-scoped-disaster-recovery-1";
    formData.dbScoped = true;
    formData.dbs = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (namespaceNames.contains(namespace.name)) {
        formData.dbs.add(namespace.namespaceUUID.toString());
      }
    }

    formData.bootstrapParams = new XClusterConfigRestartFormData.RestartBootstrapParams();
    formData.bootstrapParams.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    formData.bootstrapParams.backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();

    Result result = createDrConfig(formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    Thread.sleep(5000);
    int rowCount = getRowCount(targetUniverse, table1);
    assertEquals(1, rowCount);

    // Perform full move on source universe.
    UniverseDefinitionTaskParams.Cluster sourceCluster =
        sourceUniverse.getUniverseDetails().getPrimaryCluster();
    sourceCluster.userIntent.instanceType = INSTANCE_TYPE_CODE_2;
    PlacementInfoUtil.updateUniverseDefinition(
        sourceUniverse.getUniverseDetails(),
        customer.getId(),
        sourceCluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(sourceUniverse, 1, 1);
    UUID taskID1 =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()),
            sourceUniverse.getUniverseDetails());
    TaskInfo fullMoveTaskInfo1 = waitForTask(taskID1, sourceUniverse);
    verifyUniverseTaskSuccess(fullMoveTaskInfo1);
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    // Get new universe state.
    sourceUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());

    // Validate replication working as expected.
    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table1);
    assertEquals(2, rowCount);

    // Perform full move on target universe.
    UniverseDefinitionTaskParams.Cluster targetCluster =
        targetUniverse.getUniverseDetails().getPrimaryCluster();
    targetCluster.userIntent.instanceType = INSTANCE_TYPE_CODE_2;
    PlacementInfoUtil.updateUniverseDefinition(
        targetUniverse.getUniverseDetails(),
        customer.getId(),
        targetCluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(targetUniverse, 1, 1);
    UUID taskID2 =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(targetUniverse.getUniverseUUID()),
            targetUniverse.getUniverseDetails());
    TaskInfo fullMoveTaskInfo2 = waitForTask(taskID2, targetUniverse);
    verifyUniverseTaskSuccess(fullMoveTaskInfo2);
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
    // Get new universe state.
    targetUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());

    // Validate replication working as expected.
    insertRow(sourceUniverse, table1, Map.of("id", "3", "name", "'val3'"));
    Thread.sleep(5000);
    rowCount = getRowCount(targetUniverse, table1);
    assertEquals(3, rowCount);

    // Delete db scoped DR.
    Result deleteResult = deleteDrConfig(UUID.fromString(json.get("resourceUUID").asText()));
    assertOk(deleteResult);
    JsonNode deleteJson = Json.parse(contentAsString(deleteResult));
    TaskInfo deleteTaskInfo =
        waitForTask(
            UUID.fromString(deleteJson.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, deleteTaskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));
  }
}
