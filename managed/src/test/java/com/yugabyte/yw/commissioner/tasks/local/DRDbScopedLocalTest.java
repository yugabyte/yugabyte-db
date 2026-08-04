// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonMappingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
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
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.IOException;
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
import org.yb.client.GetXClusterOutboundReplicationGroupInfoResponse;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class DRDbScopedLocalTest extends DRLocalTestBase {

  // 2.23.0.0-b691+ version does not require enable_xcluster_api_v2 and allowed_preview_flags_csv
  //   gflags to be set.
  public static final String DB_SCOPED_STABLE_VERSION = "2024.1.3.0-b105";
  public static String DB_SCOPED_STABLE_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2024.1.3.0-b105/yugabyte-2024.1.3.0-b105-%s-%s.tar.gz";

  public static Map<String, String> dbScopedMasterGFlags =
      Map.of(
          "enable_xcluster_api_v2", "true", "allowed_preview_flags_csv", "enable_xcluster_api_v2");

  @Before
  public void setupDrDbScoped() {
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        UniverseConfKeys.dbScopedXClusterCreationEnabled.getKey(),
        "true",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        UniverseConfKeys.XClusterDbScopedAutomaticDdlCreationEnabled.getKey(),
        "false",
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
    // Wait for 5 seconds.
    try {
      Thread.sleep(5000);
    } catch (InterruptedException e) {
      e.printStackTrace();
    }

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
    setDatabasesFormData.dbs = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (updateNamespaceNames.contains(namespace.name)) {
        setDatabasesFormData.dbs.add(namespace.namespaceUUID.toString());
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

    // Need to wait for masters to propagate dropping of db1 to tservers, which will take 1-2
    // seconds.
    Thread.sleep(5000);

    // Validate rows are not replicated to target universe.
    insertRow(sourceUniverse, table1, Map.of("id", "2", "name", "'val2'"));
    validateRowCount(targetUniverse, table1, 1 /* expectedRows */);
    validateNotExpectedRowCount(targetUniverse, table1, 2 /* notExpectedRows */);

    // Validate we are able to drop database on target universe (PITR config is dropped correctly).
    dropDatabase(sourceUniverse, db1);
    dropDatabase(targetUniverse, db1);

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
  public void testDbScopedFailoverRestartWithSwitchover() throws InterruptedException, IOException {
    CreateDRMetadata createData = defaultDbDRCreate();
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

    // Insert new rows that will not be restored by PITR safetime as we have already
    //   gotten the safetime earlier.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "8", "name", "'val8'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

    // Take down source universe nodes.
    for (NodeDetails sourceNode : sourceUniverse.getNodes()) {
      killProcessOnNode(sourceUniverse.getUniverseUUID(), sourceNode.nodeName, ServerType.TSERVER);
    }

    // Get xcluster config replication group name.
    // Then check that the outbound replication group is deleted.
    String oldReplicationGroupName =
        DrConfig.getOrBadRequest(drConfigUUID).getActiveXClusterConfig().getReplicationGroupName();

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

    for (NodeDetails newTargetNode : newTargetUniverse.getNodes()) {
      startProcessesOnNode(newTargetUniverse.getUniverseUUID(), newTargetNode, ServerType.TSERVER);
    }

    // Wait for tservers to start.
    try (YBClient client =
        ybClientService.getClient(
            newTargetUniverse.getMasterAddresses(), newTargetUniverse.getCertificateNodetoNode())) {
      waitTillNumOfTservers(client, 3);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
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

    // Perform a switchover to make sure that new outbound replication group can be created even
    // though stale outbound replication group is persisted.
    DrConfigSwitchoverForm switchoverForm = new DrConfigSwitchoverForm();
    switchoverForm.primaryUniverseUuid = newTargetUniverse.getUniverseUUID();
    switchoverForm.drReplicaUniverseUuid = newSourceUniverse.getUniverseUUID();

    Result switchoverResult = switchover(drConfigUUID, switchoverForm);
    assertOk(switchoverResult);
    json = Json.parse(contentAsString(switchoverResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    sourceUniverse = Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID());
    targetUniverse = Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Check outbound replication on old source universe is deleted.
    try (YBClient client =
        ybClientService.getClient(
            newTargetUniverse.getMasterAddresses(), newTargetUniverse.getCertificateNodetoNode())) {
      try {
        GetXClusterOutboundReplicationGroupInfoResponse rgInfo =
            client.getXClusterOutboundReplicationGroupInfo(oldReplicationGroupName);
        throw new RuntimeException(
            String.format(
                "oldReplicationGroupName %s should not be found and should have been deleted",
                oldReplicationGroupName));
      } catch (Exception ignored) {
        log.debug(
            "The outbound replication group does not exist as expected {}", ignored.getMessage());
        // The outbound replication group does not exist as expected.
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "3", "name", "'val3'"));
    validateRowCount(targetUniverse, tables.get(0), 3 /* expectedRows */);

    insertRow(sourceUniverse, tables.get(1), Map.of("id", "12", "name", "'val12'"));
    validateRowCount(targetUniverse, tables.get(1), 3 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);

    for (Db db : dbs) {
      dropDatabase(sourceUniverse, db);
      dropDatabase(targetUniverse, db);
    }
  }

  @Test
  public void testDbScopedChangeReplica() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate(1, 1);
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Universe newTargetUniverse =
        createDRUniverse(DB_SCOPED_STABLE_VERSION, "new-target-universe", true);
    createTestSet(newTargetUniverse, dbs, tables);

    // Replace replica to use newly created universe.
    DrConfigReplaceReplicaForm replaceReplicaForm = new DrConfigReplaceReplicaForm();
    replaceReplicaForm.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    replaceReplicaForm.drReplicaUniverseUuid = newTargetUniverse.getUniverseUUID();
    Result replaceReplicaResult = replaceReplica(drConfigUUID, replaceReplicaForm);
    assertOk(replaceReplicaResult);
    JsonNode json = Json.parse(contentAsString(replaceReplicaResult));
    TaskInfo taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, newTargetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID()));

    // Validate new replication works.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(newTargetUniverse, tables.get(0), 2 /* expectedRows */);

    insertRow(sourceUniverse, tables.get(1), Map.of("id", "11", "name", "'val11'"));
    validateRowCount(newTargetUniverse, tables.get(1), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, newTargetUniverse);

    for (Db db : dbs) {
      dropDatabase(sourceUniverse, db);
      dropDatabase(targetUniverse, db);
      dropDatabase(newTargetUniverse, db);
    }
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

    // Insert new rows that will not be restored by PITR safetime as we have already
    //   gotten the safetime earlier.
    insertRow(sourceUniverse, tables.get(0), Map.of("id", "8", "name", "'val8'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

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

    DrConfigSwitchoverForm newSwitchoverForm = new DrConfigSwitchoverForm();
    newSwitchoverForm.primaryUniverseUuid = newTargetUniverse.getUniverseUUID();
    newSwitchoverForm.drReplicaUniverseUuid = newSourceUniverse.getUniverseUUID();

    Result newSwitchoverResult = switchover(drConfigUUID, newSwitchoverForm);
    assertOk(newSwitchoverResult);
    json = Json.parse(contentAsString(newSwitchoverResult));
    taskInfo =
        waitForTask(
            UUID.fromString(json.get("taskUUID").asText()), newSourceUniverse, newTargetUniverse);
    Universe newSourceUniverse2 = Universe.getOrBadRequest(newTargetUniverse.getUniverseUUID());
    Universe newTargetUniverse2 = Universe.getOrBadRequest(newSourceUniverse.getUniverseUUID());
    verifyUniverseState(Universe.getOrBadRequest(newSourceUniverse2.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(newTargetUniverse2.getUniverseUUID()));

    // Sleep to make sure roles are propogated properly after switchover.
    Thread.sleep(5000);

    // Validate newSourceUniverse -> newTargetUniverse replication succeeds.
    insertRow(newSourceUniverse2, tables.get(0), Map.of("id", "3", "name", "'val3'"));
    validateRowCount(newTargetUniverse2, tables.get(0), 3 /* expectedRows */);

    insertRow(newSourceUniverse2, tables.get(1), Map.of("id", "12", "name", "'val12'"));
    validateRowCount(newTargetUniverse2, tables.get(1), 3 /* expectedRows */);

    deleteDrConfig(drConfigUUID, newSourceUniverse2, newTargetUniverse2);

    // Should be able to drop dbs as PITRs are deleted.
    for (Db db : dbs) {
      dropDatabase(newSourceUniverse2, db);
      dropDatabase(newTargetUniverse2, db);
    }
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

    // Should be able to drop dbs as PITRs are deleted.
    for (Db db : dbs) {
      dropDatabase(sourceUniverse, db);
      dropDatabase(targetUniverse, db);
    }
  }

  @Test
  public void testDbScopedPauseResumeUniverses() throws InterruptedException {
    CreateDRMetadata createData = defaultDbDRCreate();
    UUID drConfigUUID = createData.drConfigUUID;
    Universe sourceUniverse = createData.sourceUniverse;
    Universe targetUniverse = createData.targetUniverse;
    List<Db> dbs = createData.dbs;
    List<Table> tables = createData.tables;

    Result pauseResult = pauseUniverses(drConfigUUID);
    JsonNode json = Json.parse(contentAsString(pauseResult));
    TaskInfo taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    Result resumeResult = resumeUniverses(drConfigUUID);
    json = Json.parse(contentAsString(resumeResult));
    taskInfo =
        waitForTask(UUID.fromString(json.get("taskUUID").asText()), sourceUniverse, targetUniverse);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(sourceUniverse.getUniverseUUID()));
    verifyUniverseState(Universe.getOrBadRequest(targetUniverse.getUniverseUUID()));

    insertRow(sourceUniverse, tables.get(0), Map.of("id", "2", "name", "'val2'"));
    validateRowCount(targetUniverse, tables.get(0), 2 /* expectedRows */);

    deleteDrConfig(drConfigUUID, sourceUniverse, targetUniverse);
  }

  @Test
  public void testDBScopedXClusterTableConfigStatus()
      throws InterruptedException, JsonMappingException, JsonProcessingException {
    Universe sourceUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION,
            "source-universe",
            true /* disableTls */,
            1 /* numNodes */,
            1 /* replicationFactor */);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(sourceUniverse);
    initYCQL(sourceUniverse);
    Universe targetUniverse =
        createDRUniverse(
            DB_SCOPED_STABLE_VERSION,
            "target-universe",
            true /* disableTls */,
            1 /* numNodes */,
            1 /* replicationFactor */);
    // Add YSQL and YCQL tables to source universe which would not be part of replication.
    initYSQL(targetUniverse);
    initYCQL(targetUniverse);

    // Set up the storage config.
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());

    // Add database and tables to the universes.
    Db db = Db.create("test_xcluster_db", false);
    Table table1 = Table.create("test_table_1", DEFAULT_TABLE_COLUMNS, db);
    Table table2 = Table.create("test_table_2", DEFAULT_TABLE_COLUMNS, db);
    Table table3 = Table.create("test_table_3", DEFAULT_TABLE_COLUMNS, db);
    Table table4 = Table.create("test_table_4", DEFAULT_TABLE_COLUMNS, db);
    createTestSet(sourceUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));
    createTestSet(targetUniverse, Arrays.asList(db), Arrays.asList(table1, table2, table3));

    // Get the namespace info for the source universe.
    List<TableInfoForm.NamespaceInfoResp> namespaceInfo =
        tableHandler.listNamespaces(customer.getUuid(), sourceUniverse.getUniverseUUID(), false);

    DrConfigCreateForm formData = new DrConfigCreateForm();
    formData.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    formData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    formData.name = "db-scoped-disaster-recovery-1";
    formData.dbs = new HashSet<String>();
    for (TableInfoForm.NamespaceInfoResp namespace : namespaceInfo) {
      if (namespace.name.equals("test_xcluster_db")) {
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

    Result drConfigResult = getDrConfig(drConfigUUID);
    ObjectMapper mapper = new ObjectMapper();
    Set<XClusterTableConfig> tableDetails =
        mapper.readValue(
            Json.parse(contentAsString(drConfigResult)).get("tableDetails").toString(),
            new TypeReference<Set<XClusterTableConfig>>() {});
    assertEquals(3, tableDetails.size());
    for (XClusterTableConfig table : tableDetails) {
      assertEquals(XClusterTableConfig.Status.Running, table.getStatus());
    }

    // Create and drop table on database in replication.
    deleteTable(sourceUniverse, table1);
    deleteTable(targetUniverse, table2);
    createTable(sourceUniverse, table4);

    drConfigResult = getDrConfig(drConfigUUID);
    assertOk(drConfigResult);
    tableDetails =
        mapper.readValue(
            Json.parse(contentAsString(drConfigResult)).get("tableDetails").toString(),
            new TypeReference<Set<XClusterTableConfig>>() {});
    assertEquals(4, tableDetails.size());
    Set<XClusterTableConfig> droppedTablesFromSource =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.DroppedFromSource);
    assertEquals(1, droppedTablesFromSource.size());
    assertEquals(
        "test_table_1", droppedTablesFromSource.iterator().next().getTargetTableInfo().tableName);
    Set<XClusterTableConfig> droppedTablesOnTarget =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.DroppedFromTarget);
    assertEquals(1, droppedTablesOnTarget.size());
    assertEquals(
        "test_table_2", droppedTablesOnTarget.iterator().next().getSourceTableInfo().tableName);
    Set<XClusterTableConfig> runningTables =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.Running);
    assertEquals(1, runningTables.size());
    assertEquals("test_table_3", runningTables.iterator().next().getSourceTableInfo().tableName);
    Set<XClusterTableConfig> extraTablesOnSource =
        getTableDetailsWithStatus(tableDetails, XClusterTableConfig.Status.ExtraTableOnSource);
    assertEquals(1, extraTablesOnSource.size());
    assertEquals(
        "test_table_4", extraTablesOnSource.iterator().next().getSourceTableInfo().tableName);
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
    // Replication group may take time to delete.
    Thread.sleep(2000);
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
