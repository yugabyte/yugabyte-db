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
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSafetimeResp;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
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
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class DRDbScopedLocalTest extends DRLocalTestBase {

  // 2.23.0.0-b691+ version does not require enable_xcluster_api_v2 and allowed_preview_flags_csv
  //   gflags to be set.
  public static final String DB_SCOPED_STABLE_VERSION = "2024.1.1.0-b137";
  public static String DB_SCOPED_STABLE_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2024.1.1.0-b137/yugabyte-2024.1.1.0-b137-%s-%s.tar.gz";

  public static Map<String, String> dbScopedMasterGFlags =
      Map.of(
          "enable_xcluster_api_v2", "true", "allowed_preview_flags_csv", "enable_xcluster_api_v2");

  @Before
  public void setupDrDbScoped() {
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.dbScopedXClusterEnabled.getKey(),
        "true",
        true);

    downloadAndSetUpYBSoftware(
        os, arch, String.format(DB_SCOPED_STABLE_VERSION_URL, os, arch), DB_SCOPED_STABLE_VERSION);
    ybVersion = DB_SCOPED_STABLE_VERSION;
    ybBinPath = deriveYBBinPath(ybVersion);
    log.debug("YB version {} bin path new {}", ybVersion, ybBinPath);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(
        DB_SCOPED_STABLE_VERSION,
        getMetadataJson(DB_SCOPED_STABLE_VERSION, false).get(DB_SCOPED_STABLE_VERSION));
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
    userIntent.specificGFlags = SpecificGFlags.construct(dbScopedMasterGFlags, GFLAGS);
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
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Table> tables = createData.tables;

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);

    // Inserting values should not be replicated since replication is deleted.
    Table table1 = tables.get(0);
    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);
  }

  @Test
  public void testDrDbScopedSetupWithBootstrap() throws InterruptedException {
    Universe sourceUniverse = createDRUniverse(DB_SCOPED_STABLE_VERSION, "source-universe", true);
    Universe targetUniverse = createDRUniverse(DB_SCOPED_STABLE_VERSION, "target-universe", true);

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
    validateRowCount(sourceUniverse, table1, 1 /* expectedRows */);
    validateRowCount(sourceUniverse, table4, 1 /* expectedRows */);

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
    UUID drConfigUUID = UUID.fromString(json.get("resourceUUID").asText());

    // Check backup/restore objects are created.
    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), sourceUniverse.getUniverseUUID());
    assertEquals(2, backups.size());

    List<Restore> restores =
        Restore.fetchByUniverseUUID(customer.getUuid(), targetUniverse.getUniverseUUID());
    assertEquals(2, restores.size());

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "3", "name", "'val3'"));
    validateRowCount(targetUniverse, table1, 2 /* expectedRows */);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    validateRowCount(targetUniverse, table2, 1 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);
  }

  @Test
  public void testDrDbScopedUpdate() throws InterruptedException {
    Universe sourceUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "source-universe", true, 1, 1);
    Universe targetUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "target-universe", true, 1, 1);

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
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    validateRowCount(targetUniverse, table2, 0 /* expectedRows */);

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

    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);

    insertRow(sourceUniverse, table2, Map.of("id", "12", "name", "'val12'"));
    validateRowCount(targetUniverse, table2, 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);
  }

  @Test
  public void testDrDbScopedWithTLS() throws InterruptedException {
    Universe sourceUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION,
            "source-universe",
            false,
            1 /* numNodes */,
            1 /* replicationFactor */);
    Universe targetUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION,
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
    UUID drConfigUUID = UUID.fromString(json.get("resourceUUID").asText());

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);

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
    validateRowCount(targetUniverse, table1, 2 /* expectedRows */);

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
    validateRowCount(targetUniverse, table1, 3 /* expectedRows */);

    // Delete db scoped DR.
    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);
  }

  @Test
  public void testDbScopedFailoverRestart() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    // TODO: Simulate source universe down failure.

    Result safetimeResult = getSafeTime(drConfigUUID);
    JsonNode safeTimeJson = (ObjectNode) Json.parse(contentAsString(safetimeResult));
    DrConfigSafetimeResp safeTimeResp = Json.fromJson(safeTimeJson, DrConfigSafetimeResp.class);

    // Get the namespace info for the target universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), targetUniverse.getUniverseUUID(), false);

    Set<String> targetDbIds = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      for (Db db : dbs) {
        if (db.name.equals(namespace.name)) {
          targetDbIds.add(Util.getIdRepresentation(namespace.namespaceUUID));
          break;
        }
      }
    }

    assertEquals(dbs.size(), targetDbIds.size());
    assertEquals(targetDbIds.size(), safeTimeResp.safetimes.size());

    // Failover DR config.
    DrConfigFailoverForm drFailoverForm = new DrConfigFailoverForm();
    drFailoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    drFailoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();
    Map<String, Long> namespaceIdSafetimeEpochUsMap = new HashMap<>();
    for (DrConfigSafetimeResp.NamespaceSafetime namespaceSafetime : safeTimeResp.safetimes) {
      if (targetDbIds.contains(namespaceSafetime.getNamespaceId())) {
        namespaceIdSafetimeEpochUsMap.put(
            namespaceSafetime.getNamespaceId(), namespaceSafetime.getSafetimeEpochUs());
      }
    }
    assertEquals(2, namespaceIdSafetimeEpochUsMap.size());
    drFailoverForm.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
    Result failoverResult = failover(drConfigUUID, drFailoverForm);
    assertOk(failoverResult);
    JsonNode json = Json.parse(contentAsString(failoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());
    verifyUniverseState(newSourceUniverse);
    verifyUniverseState(newTargetUniverse);

    // Repair DR config.
    DrConfigRestartForm restartForm = new DrConfigRestartForm();
    restartForm.dbs = new HashSet<String>();
    Result repairResult = restart(drConfigUUID, restartForm);
    assertOk(repairResult);
    json = Json.parse(contentAsString(repairResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse, newTargetUniverse);
  }

  @Test
  public void testDbScopedFailoverChangeReplica() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate(1, 1);
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Result safetimeResult = getSafeTime(drConfigUUID);
    JsonNode safeTimeJson = (ObjectNode) Json.parse(contentAsString(safetimeResult));
    DrConfigSafetimeResp safeTimeResp = Json.fromJson(safeTimeJson, DrConfigSafetimeResp.class);

    // Get the namespace info for the target universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), targetUniverse.getUniverseUUID(), false);

    Set<String> targetDbIds = new HashSet<>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      for (Db db : dbs) {
        if (db.name.equals(namespace.name)) {
          targetDbIds.add(Util.getIdRepresentation(namespace.namespaceUUID));
          break;
        }
      }
    }

    assertEquals(dbs.size(), targetDbIds.size());
    assertEquals(targetDbIds.size(), safeTimeResp.safetimes.size());

    // Failover DR config.
    DrConfigFailoverForm drFailoverForm = new DrConfigFailoverForm();
    drFailoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    drFailoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();
    Map<String, Long> namespaceIdSafetimeEpochUsMap = new HashMap<>();
    for (DrConfigSafetimeResp.NamespaceSafetime namespaceSafetime : safeTimeResp.safetimes) {
      if (targetDbIds.contains(namespaceSafetime.getNamespaceId())) {
        namespaceIdSafetimeEpochUsMap.put(
            namespaceSafetime.getNamespaceId(), namespaceSafetime.getSafetimeEpochUs());
      }
    }
    assertEquals(2, namespaceIdSafetimeEpochUsMap.size());
    drFailoverForm.namespaceIdSafetimeEpochUsMap = namespaceIdSafetimeEpochUsMap;
    Result failoverResult = failover(drConfigUUID, drFailoverForm);
    assertOk(failoverResult);
    JsonNode json = Json.parse(contentAsString(failoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "new-target-universe", true);
    createTestSet(newTargetUniverse, dbs, tables);

    // Replace replica to use newly created universe.
    DrConfigReplaceReplicaForm replaceReplicaForm = new DrConfigReplaceReplicaForm();
    replaceReplicaForm.primaryUniverseUuid = newSourceUniverse.getUniverseUUID();
    replaceReplicaForm.drReplicaUniverseUuid = newTargetUniverse.getUniverseUUID();
    Result replaceReplicaResult = replaceReplica(drConfigUUID, replaceReplicaForm);
    assertOk(replaceReplicaResult);
    json = Json.parse(contentAsString(replaceReplicaResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate new replication works.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse, newTargetUniverse);
  }

  @Test
  public void testDbScopedSwitchover() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    DrConfigSwitchoverForm switchoverForm = new DrConfigSwitchoverForm();
    switchoverForm.primaryUniverseUuid = targetUniverse.getUniverseUUID();
    switchoverForm.drReplicaUniverseUuid = sourceUniverse.getUniverseUUID();

    Result switchoverResult = switchover(drConfigUUID, switchoverForm);
    assertOk(switchoverResult);
    JsonNode json = Json.parse(contentAsString(switchoverResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    Universe newSourceUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    Universe newTargetUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(newSourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse, newTargetUniverse);
  }

  @Test
  public void testDbScopedRestart() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    DrConfigRestartForm restartForm = new DrConfigRestartForm();
    restartForm.dbs = new HashSet<String>();
    Result restartResult = restart(drConfigUUID, restartForm);
    assertOk(restartResult);
    JsonNode json = Json.parse(contentAsString(restartResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    insertRow(sourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(sourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(targetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);
  }

  private void deleteDrConfig(UUID drConfigUUID, Universe sourceUniverse, Universe targetUniverse)
      throws InterruptedException {
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

  private CreateDRMetadata defaultDbDRCreate() throws InterruptedException {
    return defaultDbDRCreate(3, 3);
  }

  /**
   * @param numNodes the number of nodes to create for each of source and target universe
   * @param replicationFactor the replication factor of source and target universe
   * @return metadata consisting of dbs, tables, source/target universes, dr config uuid
   * @throws InterruptedException
   */
  private CreateDRMetadata defaultDbDRCreate(int numNodes, int replicationFactor)
      throws InterruptedException {
    Universe sourceUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION, "source-universe", true, numNodes, replicationFactor);
    Universe targetUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION, "target-universe", true, numNodes, replicationFactor);

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
    UUID drConfigUUID = UUID.fromString(json.get("resourceUUID").asText());

    // Insert values into source universe and make sure they are replicated on target.
    insertRow(sourceUniverse, table1, Map.of("id", "1", "name", "'val1'"));
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);

    insertRow(sourceUniverse, table2, Map.of("id", "10", "name", "'val10'"));
    validateRowCount(targetUniverse, table2, 1 /* expectedRows */);

    return new CreateDRMetadata(
        dbs,
        tables,
        Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()),
        Universe.getOrBadRequest(targetUniverse.getUniverseUUID()),
        drConfigUUID);
  }

  public static class CreateDRMetadata {
    public List<Db> dbs;
    public List<Table> tables;
    public Universe sourceUniverse;
    public Universe targetUniverse;
    public UUID drConfigUUID;

    public CreateDRMetadata(
        List<Db> dbs,
        List<Table> tables,
        Universe sourceUniverse,
        Universe targetUniverse,
        UUID drConfigUUID) {
      this.dbs = dbs;
      this.tables = tables;
      this.sourceUniverse = sourceUniverse;
      this.targetUniverse = targetUniverse;
      this.drConfigUUID = drConfigUUID;
    }
  }
}
