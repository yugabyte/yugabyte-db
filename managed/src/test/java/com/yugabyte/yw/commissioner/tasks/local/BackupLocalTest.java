// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupRequestParams.KeyspaceTable;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

@Slf4j
public class BackupLocalTest extends LocalProviderUniverseTestBase {
  private final String DUMP_CHECK_URL =
      "https://s3.us-west-2.amazonaws.com/releases.yugabyte.com/2.25.2.0-b275/"
          + "yugabyte-2.25.2.0-b275-centos-x86_64.tar.gz";

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(90, 120);
  }

  private BackupRequestParams getBackupParams(Universe universe, CustomerConfig customerConfig) {
    BackupRequestParams params = new BackupRequestParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.backupType = TableType.PGSQL_TABLE_TYPE;
    params.customerUUID = customer.getUuid();
    params.storageConfigUUID = customerConfig.getConfigUUID();
    params.timeBeforeDelete = 8640000L;
    params.expiryTimeUnit = TimeUnit.DAYS;
    params.setDumpRoleChecks(true);

    return params;
  }

  private RestoreBackupParams getRestoreParams(
      Universe universe, Backup backup, CustomerConfig customerConfig, String keySpace) {
    RestoreBackupParams rParams = new RestoreBackupParams();
    rParams.setUniverseUUID(universe.getUniverseUUID());
    rParams.customerUUID = customer.getUuid();
    rParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    rParams.storageConfigUUID = customerConfig.getConfigUUID();
    rParams.category = Backup.BackupCategory.YB_CONTROLLER;
    RestoreBackupParams.BackupStorageInfo storageInfo = new RestoreBackupParams.BackupStorageInfo();
    storageInfo.backupType = TableType.PGSQL_TABLE_TYPE;
    storageInfo.keyspace = keySpace;
    storageInfo.storageLocation = backup.getBackupInfo().backupList.get(0).storageLocation;
    List<RestoreBackupParams.BackupStorageInfo> storageInfoList = new ArrayList<>();
    storageInfoList.add(storageInfo);
    rParams.backupStorageInfoList = storageInfoList;
    rParams.category = Backup.BackupCategory.YB_CONTROLLER;

    return rParams;
  }

  private void providerSetup() {
    if (Util.compareYBVersions(
            DB_VERSION,
            YbcBackupUtil.YBDB_STABLE_GRANT_SAFETY_VERSION,
            YbcBackupUtil.YBDB_PREVIEW_GRANT_SAFETY_VERSION,
            true)
        < 0) {
      ybVersion = YbcBackupUtil.YBDB_PREVIEW_GRANT_SAFETY_VERSION;
      log.info("Setting DB_VERSION to {} for testYBCBackupDumpRolesCheck", ybVersion);
      downloadAndSetUpYBSoftware(os, arch, DUMP_CHECK_URL, ybVersion);
      ObjectNode releases =
          (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
      releases.set(ybVersion, getMetadataJson(ybVersion, false).get(ybVersion));
      YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
      ybBinPath = deriveYBBinPath(ybVersion);
      LocalCloudInfo localCloudInfo = new LocalCloudInfo();
      localCloudInfo.setDataHomeDir(
          ((LocalCloudInfo) CloudInfoInterface.get(provider)).getDataHomeDir());
      localCloudInfo.setYugabyteBinDir(ybBinPath);
      localCloudInfo.setYbcBinDir(ybcBinPath);
      ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
      cloudInfo.setLocal(localCloudInfo);
      ProviderDetails providerDetails = new ProviderDetails();
      providerDetails.setCloudInfo(cloudInfo);
      provider.setDetails(providerDetails);
      provider.update();
    }
  }

  @Test
  public void testYBCBackup() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe universe = createUniverseWithYbc(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());
    BackupRequestParams params = getBackupParams(universe, customerConfig);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), params);
    TaskInfo taskInfo = waitForTask(taskUUID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
    Backup backup = backups.get(0);

    // Restoring the backup on the same universe under a different keyspace.
    RestoreBackupParams rParams = getRestoreParams(universe, backup, customerConfig, "yb_restore");
    taskUUID = backupHelper.createRestoreTask(customer.getUuid(), rParams);
    taskInfo = waitForTask(taskUUID, universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(universe);
    verifyYSQL(universe, false, "yb_restore");
    verifyPayload();
  }

  @Test
  public void testYBCBackuponDifferentUniverse() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent("universe-1", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    initYSQL(source);
    initAndStartPayload(source);
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    log.info("Customer config here: {}", customerConfig.toString());
    BackupRequestParams params = getBackupParams(source, customerConfig);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), params);
    TaskInfo taskInfo = waitForTask(taskUUID, source);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<Backup> backups = Backup.fetchByUniverseUUID(customer.getUuid(), source.getUniverseUUID());
    Backup backup = backups.get(0);

    userIntent = getDefaultUserIntent("universe-2", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);

    // Restoring the backup on the same universe under a different keyspace.
    String db2Name = YUGABYTE_DB + "_2";
    RestoreBackupParams rParams = getRestoreParams(target, backup, customerConfig, db2Name);
    taskUUID = backupHelper.createRestoreTask(customer.getUuid(), rParams);
    taskInfo = waitForTask(taskUUID, source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(target, false, db2Name);
    verifyPayload();
  }

  /*
   * Validate the dump_role_checks feature work by restoring to a new universe without old roles.
   * We must also pass ON_ERROR_STOP=1 to the restore process
   */
  @Test
  public void testYBCBackupDumpRolesCheck() throws InterruptedException {
    providerSetup();
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent("universe-1", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe source = createUniverseWithYbc(userIntent);
    NodeDetails nd =
        source.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .orElse(null);
    boolean authEnabled =
        source.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    List<String> psqlCmds = new ArrayList<>();
    psqlCmds.add("CREATE ROLE role1;");
    psqlCmds.add("CREATE ROLE role2;");
    psqlCmds.add("Create TABLE t1(c1 int);");
    psqlCmds.add("GRANT ALL ON t1 TO role1;");
    psqlCmds.add("ALTER TABLE t1 OWNER TO role2;");
    psqlCmds.add("CREATE TABLE t2(c1 int);");
    psqlCmds.add("GRANT ALL ON t2 TO role2;");
    psqlCmds.add("CREATE TABLE t3(c1 int);");
    psqlCmds.add("GRANT ALL ON t3 TO role2;");
    psqlCmds.add("ALTER TABLE t3 OWNER TO role1;");
    psqlCmds.add("SET SESSION AUTHORIZATION role1;");
    ShellResponse resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, source, YUGABYTE_DB, String.join(" ", psqlCmds), 10, authEnabled);
    if (!resp.isSuccess()) {
      log.error("Failed to run psql commands: {}", resp.message);
    }
    assertTrue(resp.isSuccess());

    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    BackupRequestParams params = getBackupParams(source, customerConfig);
    params.setDumpRoleChecks(true);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), params);
    TaskInfo taskInfo = waitForTask(taskUUID, source);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    List<Backup> backups = Backup.fetchByUniverseUUID(customer.getUuid(), source.getUniverseUUID());
    Backup backup = backups.get(0);

    userIntent = getDefaultUserIntent("universe-2", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybcFlags = getYbcGFlags(userIntent);
    Universe target = createUniverseWithYbc(userIntent);
    nd =
        target.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .orElse(null);
    psqlCmds.clear();
    psqlCmds.add("CREATE ROLE role2;");
    assertTrue(
        localNodeUniverseManager
            .runYsqlCommand(nd, target, YUGABYTE_DB, String.join(" ", psqlCmds), 10, authEnabled)
            .isSuccess());
    String db2Name = YUGABYTE_DB + "_2";
    RestoreBackupParams rParams = getRestoreParams(target, backup, customerConfig, db2Name);
    rParams.backupStorageInfoList.stream()
        .forEach(backupStorage -> backupStorage.setIgnoreErrors(false));
    for (BackupStorageInfo backupStorage : rParams.backupStorageInfoList) {
      backupStorage.setIgnoreErrors(true);
    }
    taskUUID = backupHelper.createRestoreTask(customer.getUuid(), rParams);
    taskInfo = waitForTask(taskUUID, source, target);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    // Target Universe should have t1, t2, and t3
    // role 1 grants should not have happened - t1
    psqlCmds.clear();
    psqlCmds.add(
        "SELECT privilege_type FROM information_schema.role_table_grants WHERE grantee = 'role1'"
            + " AND table_name = 't1';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, db2Name, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    String result = resp.message;
    assertFalse(result.contains("INSERT"));

    // role 2 grants should have happened
    psqlCmds.clear();
    psqlCmds.add(
        "SELECT privilege_type FROM information_schema.role_table_grants WHERE grantee = 'role2'"
            + " AND table_name = 't2';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, db2Name, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    assertTrue(result.contains("INSERT"));

    // Validate t3 is not owned by role 1
    psqlCmds.clear();
    psqlCmds.add("SELECT tableowner FROM pg_tables WHERE tablename = 't3';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, db2Name, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    assertFalse(result.contains("role1")); // role 1 was not created

    // Validate t1 is owned by role 2
    psqlCmds.clear();
    psqlCmds.add("SELECT tableowner FROM pg_tables WHERE tablename = 't1';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, db2Name, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    assertTrue(result.contains("role2"));
    assertFalse(result.contains("yugabyte"));
  }

  @Test
  public void testYBCBackupUseRoles() {
    providerSetup();
    YbcBackupTestParams params = new YbcBackupTestParams();
    params.useRoles = true;
    Universe source = null;
    Universe target = null;
    try {
      UniverseDefinitionTaskParams.UserIntent userIntent =
          getDefaultUserIntent("universe-1", false);
      userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
      userIntent.ybcFlags = getYbcGFlags(userIntent);
      source = createUniverseWithYbc(userIntent);
      userIntent = getDefaultUserIntent("universe-2", false);
      userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
      userIntent.ybcFlags = getYbcGFlags(userIntent);
      target = createUniverseWithYbc(userIntent);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
    runYbcBackupTest(source, target, params);
  }

  @Test
  public void testYbcBackupUseRolesErrorIfRolesExists() {
    providerSetup();
    YbcBackupTestParams params = new YbcBackupTestParams();
    params.useRoles = true;
    params.errorIfRolesExists = true;
    try {
      UniverseDefinitionTaskParams.UserIntent userIntent =
          getDefaultUserIntent("universe-1", false);
      userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
      userIntent.ybcFlags = getYbcGFlags(userIntent);
      Universe source = createUniverseWithYbc(userIntent);
      userIntent = getDefaultUserIntent("universe-2", false);
      userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
      userIntent.ybcFlags = getYbcGFlags(userIntent);
      Universe target = createUniverseWithYbc(userIntent);
      initYSQL(target);
      NodeDetails nd =
          target.getUniverseDetails().nodeDetailsSet.stream()
              .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
              .findFirst()
              .orElse(null);
      boolean authEnabled =
          target.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
      localNodeUniverseManager.runYsqlCommand(
          nd, target, params.targetDbName, "CREATE ROLE role1;", 10, authEnabled);
      localNodeUniverseManager.runYsqlCommand(
          nd, target, params.targetDbName, "CREATE ROLE role2;", 10, authEnabled);
      RuntimeException exception =
          assertThrows(RuntimeException.class, () -> runYbcBackupTest(source, target, params));
      assertTrue(exception.getMessage().contains("restore failed"));
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  private class YbcBackupTestParams {
    /*********************************/
    /* Backup and restore parameters */
    /*********************************/
    private boolean useRoles = false;

    private boolean usePrivileges = true;
    private boolean dumpRoleChecks = true;
    private boolean ignoreErrors = false;
    private boolean errorIfRolesExists = false;

    /***********************************************************/
    /* Database names for source (backup) and target (restore) */
    /***********************************************************/
    private String srcDbName = YUGABYTE_DB;

    private String targetDbName = YUGABYTE_DB + "_2";

    /***********************************/
    /* How to check results of restore */
    /***********************************/
    // Privileges are set (grants and alters). This requires the role to exist on the target (either
    // through backup/restore or manual creation) and usePrivileges to be true.
    private boolean privilegesShouldExist = true;
  }

  private void runYbcBackupTest(Universe source, Universe target, YbcBackupTestParams params) {
    // Initial setup before backup
    initYSQL(source);
    initAndStartPayload(source);
    NodeDetails nd =
        source.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .orElse(null);
    boolean authEnabled =
        source.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    List<String> psqlCmds = new ArrayList<>();
    psqlCmds.add("CREATE TABLE t1(c1 int);");
    psqlCmds.add("CREATE TABLE t2(c1 int);");
    psqlCmds.add("CREATE ROLE role1;");
    psqlCmds.add("CREATE ROLE role2;");
    psqlCmds.add("GRANT ALL ON t1 TO role1;");
    psqlCmds.add("GRANT ALL ON t2 TO role2;");
    psqlCmds.add("ALTER TABLE t1 OWNER TO role1;");
    psqlCmds.add("ALTER TABLE t2 OWNER TO role2;");
    psqlCmds.add("INSERT INTO t1 VALUES (1);");
    psqlCmds.add("INSERT INTO t2 VALUES (2);");
    ShellResponse resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, source, params.srcDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());

    // Take and restore the backup
    CustomerConfig customerConfig =
        ModelFactory.createNfsStorageConfig(customer, "test_nfs_storage", getBackupBaseDirectory());
    Backup backup = takeYbcBackup(source, params, customerConfig);
    restoreYbcBackup(target, params, customerConfig, backup);
    nd =
        target.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
            .findFirst()
            .orElse(null);
    authEnabled = target.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    verifyYSQL(target, false, params.targetDbName);

    // Basic checks.
    psqlCmds.clear();
    psqlCmds.add("SELECT * FROM t1;");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    String result = resp.message;
    assertTrue(result.contains("1"));

    psqlCmds.clear();
    psqlCmds.add("SELECT * FROM t2;");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    assertTrue(result.contains("2"));

    // Check roles restored
    if (params.useRoles) {
      psqlCmds.clear();
      psqlCmds.add("SELECT rolname FROM pg_roles;");
      resp =
          localNodeUniverseManager.runYsqlCommand(
              nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
      assertTrue(resp.isSuccess());
      result = resp.message;
      assertTrue(result.contains("role1"));
      assertTrue(result.contains("role2"));
    }
    // Check privileges
    psqlCmds.clear();
    psqlCmds.add("SELECT tableowner FROM pg_tables WHERE tablename = 't1';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    if (params.privilegesShouldExist) {
      assertTrue(result.contains("role1"));
      assertFalse(result.contains("yugabyte"));
    } else {
      assertFalse(result.contains("role1"));
      assertTrue(result.contains("yugabyte"));
    }
    psqlCmds.clear();
    psqlCmds.add("SELECT tableowner FROM pg_tables WHERE tablename = 't2';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    if (params.privilegesShouldExist) {
      assertTrue(result.contains("role2"));
      assertFalse(result.contains("yugabyte"));
    } else {
      assertFalse(result.contains("role2"));
      assertTrue(result.contains("yugabyte"));
    }

    psqlCmds.clear();
    psqlCmds.add(
        "SELECT privilege_type FROM information_schema.role_table_grants WHERE grantee ="
            + " 'role1' AND table_name = 't1';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    if (params.privilegesShouldExist) {
      assertTrue(result.contains("INSERT"));
    } else {
      assertFalse(result.contains("INSERT"));
    }
    psqlCmds.clear();
    psqlCmds.add(
        "SELECT privilege_type FROM information_schema.role_table_grants WHERE grantee ="
            + " 'role2' AND table_name = 't2';");
    resp =
        localNodeUniverseManager.runYsqlCommand(
            nd, target, params.targetDbName, String.join(" ", psqlCmds), 10, authEnabled);
    assertTrue(resp.isSuccess());
    result = resp.message;
    if (params.privilegesShouldExist) {
      assertTrue(result.contains("INSERT"));
    } else {
      assertFalse(result.contains("INSERT"));
    }
  }

  private Backup takeYbcBackup(
      Universe universe, YbcBackupTestParams params, CustomerConfig customerConfig) {
    BackupRequestParams backupParams = new BackupRequestParams();
    backupParams.setUniverseUUID(universe.getUniverseUUID());
    backupParams.backupType = TableType.PGSQL_TABLE_TYPE;
    backupParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupParams.setUseRoles(params.useRoles);
    backupParams.setUsePrivileges(params.usePrivileges);
    backupParams.setDumpRoleChecks(params.dumpRoleChecks);
    KeyspaceTable keyspaceTable = new KeyspaceTable();
    keyspaceTable.keyspace = params.srcDbName;
    backupParams.keyspaceTableList = new ArrayList<>();
    backupParams.keyspaceTableList.add(keyspaceTable);
    UUID taskUUID = backupHelper.createBackupTask(customer.getUuid(), backupParams);
    try {
      TaskInfo taskInfo = waitForTask(taskUUID, universe);
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
    Backup backup = backups.get(0);
    return backup;
  }

  private void restoreYbcBackup(
      Universe universe, YbcBackupTestParams params, CustomerConfig customerConfig, Backup backup) {
    RestoreBackupParams restoreParams = new RestoreBackupParams();
    restoreParams.setUniverseUUID(universe.getUniverseUUID());
    restoreParams.customerUUID = customer.getUuid();
    restoreParams.actionType = RestoreBackupParams.ActionType.RESTORE;
    restoreParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreParams.category = Backup.BackupCategory.YB_CONTROLLER;
    restoreParams.backupStorageInfoList = new ArrayList<>();
    BackupStorageInfo backupStorageInfo = new BackupStorageInfo();
    backupStorageInfo.keyspace = params.targetDbName;
    backupStorageInfo.storageLocation = backup.getBackupInfo().backupList.get(0).storageLocation;
    backupStorageInfo.setUseRoles(params.useRoles);
    backupStorageInfo.setUsePrivileges(params.usePrivileges);
    backupStorageInfo.setIgnoreErrors(params.ignoreErrors);
    backupStorageInfo.setErrorIfRolesExists(params.errorIfRolesExists);
    backupStorageInfo.backupType = TableType.PGSQL_TABLE_TYPE;
    restoreParams.backupStorageInfoList.add(backupStorageInfo);
    UUID taskUUID = backupHelper.createRestoreTask(customer.getUuid(), restoreParams);
    try {
      TaskInfo taskInfo = waitForTask(taskUUID, universe);
      if (!TaskInfo.State.Success.equals(taskInfo.getTaskState())) {
        throw new RuntimeException("restore failed");
      }
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }
}
