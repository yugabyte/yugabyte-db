// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.TableInfoForm;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData;
import com.yugabyte.yw.models.Provider;
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
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public abstract class DRDbScopedLocalTestBase extends DRLocalTestBase {

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
    Provider provider = Util.getSingleProvider(userIntent);
    LocalCloudInfo localCloudInfo = provider.getDetails().getCloudInfo().getLocal();
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    provider.update();

    userIntent.ybcFlags = getYbcGFlags(userIntent);
    return createUniverseWithYbc(userIntent);
  }

  protected void deleteDrConfig(UUID drConfigUUID, Universe sourceUniverse, Universe targetUniverse)
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

  protected CreateDRMetadata defaultDbDRCreate() throws InterruptedException {
    return defaultDbDRCreate(3, 3);
  }

  /**
   * @param numNodes the number of nodes to create for each of source and target universe
   * @param replicationFactor the replication factor of source and target universe
   * @return metadata consisting of dbs, tables, source/target universes, dr config uuid
   * @throws InterruptedException
   */
  protected CreateDRMetadata defaultDbDRCreate(int numNodes, int replicationFactor)
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
