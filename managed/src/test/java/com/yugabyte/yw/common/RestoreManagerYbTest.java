// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.DevopsBase.PY_WRAPPER;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.BACKUP_SCRIPT;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.K8S_CERT_PATH;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.VM_CERT_DIR;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.ActionType;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class RestoreManagerYbTest extends FakeDBApplication {

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock RuntimeConfigFactory runtimeConfigFactory;

  @Mock RuntimeConfGetter mockConfGetter;

  @Mock Config mockConfig;

  @InjectMocks RestoreManagerYb restoreManagerYb;

  private Provider testProvider;
  private Customer testCustomer;
  private Universe testUniverse;
  private Backup testBackup;
  private String keyCode = "demo-access";
  private String pkPath = "/path/to/private.key";

  private List<UUID> getMockRegionUUIDs(int numRegions) {
    List<UUID> regionUUIDs = new LinkedList<>();
    for (int i = 0; i < numRegions; ++i) {
      String regionCode = "region-" + Integer.toString(i);
      String regionName = "Foo Region " + Integer.toString(i);
      String azCode = "PlacementAZ-" + Integer.toString(i);
      String azName = "PlacementAZ " + Integer.toString(i);
      String subnetName = "Subnet - " + Integer.toString(i);
      Region r = Region.create(testProvider, regionCode, regionName, "default-image");
      AvailabilityZone.createOrThrow(r, azCode, azName, subnetName);
      regionUUIDs.add(r.getUuid());
    }
    return regionUUIDs;
  }

  private void setupUniverse(Provider p) {
    setupUniverse(p, "0.0.1");
  }

  private void setupUniverse(Provider p, String softwareVersion) {
    setupUniverse(p, softwareVersion, false);
  }

  private void setupUniverse(Provider p, String softwareVersion, boolean enableTLS) {
    testProvider = p;
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = pkPath;
    keyInfo.sshPort = 3333;
    if (AccessKey.get(testProvider.getUuid(), keyCode) == null) {
      AccessKey.create(testProvider.getUuid(), keyCode, keyInfo);
    }

    UniverseDefinitionTaskParams uniParams = new UniverseDefinitionTaskParams();
    uniParams.nodePrefix = "yb-1-" + testUniverse.getName();
    UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.accessKeyCode = keyCode;
    userIntent.ybSoftwareVersion = softwareVersion;
    userIntent.numNodes = 3;
    userIntent.provider = testProvider.getUuid().toString();
    userIntent.replicationFactor = 3;
    userIntent.regionList = getMockRegionUUIDs(3);
    // userIntent.enableYSQLAuth = false;
    if (enableTLS) {
      userIntent.enableNodeToNodeEncrypt = true;
    }
    uniParams.upsertPrimaryCluster(userIntent, null);
    testUniverse.setUniverseDetails(uniParams);
    testUniverse =
        Universe.saveDetails(
            testUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, uniParams.nodePrefix));
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = createUniverse("Universe-1", testCustomer.getId());
    when(runtimeConfigFactory.forUniverse(any())).thenReturn(mockConfig);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.disableXxHashChecksum))).thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.backupLogVerbose)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.useServerBroadcastAddressForYbBackup)))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.enableSSE)))
        .thenReturn(false);
  }

  @Test
  public void testRestoreS3Backup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST103");
    BackupTableParams backupParams = getBackupUniverseParams(storageConfig.getConfigUUID());
    testBackup = Backup.create(testCustomer.getUuid(), backupParams);
    RestoreBackupParams restoreBackupParams =
        getRestoreBackupParams(
            ActionType.RESTORE,
            storageConfig.getConfigUUID(),
            testBackup.getBackupInfo().backupList.get(0).storageLocation);
    List<String> expectedCommand =
        getExpectedRestoreBackupCommand(restoreBackupParams, ActionType.RESTORE, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    restoreManagerYb.runCommand(restoreBackupParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testRestoreNfsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST37");
    BackupTableParams backupParams = getBackupUniverseParams(storageConfig.getConfigUUID());
    testBackup = Backup.create(testCustomer.getUuid(), backupParams);
    RestoreBackupParams restoreBackupParams =
        getRestoreBackupParams(
            ActionType.RESTORE,
            storageConfig.getConfigUUID(),
            testBackup.getBackupInfo().backupList.get(0).storageLocation);
    List<String> expectedCommand =
        getExpectedRestoreBackupCommand(restoreBackupParams, ActionType.RESTORE, "nfs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    restoreManagerYb.runCommand(restoreBackupParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testRestoreGcsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createGcsStorageConfig(testCustomer, "TEST51");
    BackupTableParams backupParams = getBackupUniverseParams(storageConfig.getConfigUUID());
    testBackup = Backup.create(testCustomer.getUuid(), backupParams);
    RestoreBackupParams restoreBackupParams =
        getRestoreBackupParams(
            ActionType.RESTORE,
            storageConfig.getConfigUUID(),
            testBackup.getBackupInfo().backupList.get(0).storageLocation);
    List<String> expectedCommand =
        getExpectedRestoreBackupCommand(restoreBackupParams, ActionType.RESTORE, "gcs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    restoreManagerYb.runCommand(restoreBackupParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testRestoreUniverseBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST39");
    BackupTableParams backupParams = getBackupUniverseParams(storageConfig.getConfigUUID());
    testBackup = Backup.create(testCustomer.getUuid(), backupParams);
    RestoreBackupParams restoreBackupParams =
        getRestoreBackupParams(
            ActionType.RESTORE,
            storageConfig.getConfigUUID(),
            testBackup.getBackupInfo().backupList.get(0).storageLocation);
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    restoreManagerYb.runCommand(restoreBackupParams);
    List<String> expectedCommand =
        getExpectedRestoreBackupCommand(restoreBackupParams, ActionType.RESTORE, "nfs");
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  private RestoreBackupParams getRestoreBackupParams(
      ActionType actionType, UUID configUUID, String storageLocation) {
    RestoreBackupParams restoreParams = new RestoreBackupParams();
    List<BackupStorageInfo> backupStorageInfoList = new ArrayList<>();

    restoreParams.setUniverseUUID(testUniverse.getUniverseUUID());
    restoreParams.parallelism = 3;
    restoreParams.storageConfigUUID = configUUID;
    restoreParams.actionType = actionType;
    restoreParams.backupStorageInfoList = backupStorageInfoList;

    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.keyspace = "mock_ks";
    storageInfo.storageLocation = storageLocation;
    backupStorageInfoList.add(storageInfo);
    return restoreParams;
  }

  private RestoreBackupParams gRestoreBackupParams(
      ActionType actionType, UUID configUUID, String storageLocation, List<String> tableNameList) {
    RestoreBackupParams restoreParams = new RestoreBackupParams();
    List<BackupStorageInfo> backupStorageInfoList = new ArrayList<>();

    restoreParams.setUniverseUUID(testUniverse.getUniverseUUID());
    restoreParams.parallelism = 3;
    restoreParams.storageConfigUUID = configUUID;
    restoreParams.actionType = actionType;
    restoreParams.backupStorageInfoList = backupStorageInfoList;

    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.keyspace = "mock_ks";
    storageInfo.tableNameList = tableNameList;
    storageInfo.storageLocation = storageLocation;
    backupStorageInfoList.add(storageInfo);
    return restoreParams;
  }

  private List<String> getExpectedRestoreBackupCommand(
      RestoreBackupParams restoreParams, ActionType actionType, String storageType) {
    AccessKey accessKey = AccessKey.get(testProvider.getUuid(), keyCode);
    Map<String, Map<String, String>> podAddrToConfig = new HashMap<>();
    UserIntent userIntent = testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;

    if (testProvider.getCode().equals("kubernetes")) {
      PlacementInfo pi = testUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
      podAddrToConfig =
          KubernetesUtil.getKubernetesConfigPerPod(
              pi, testUniverse.getUniverseDetails().nodeDetailsSet);
    }

    List<String> cmd = new LinkedList<>();
    cmd.add(PY_WRAPPER);
    cmd.add(BACKUP_SCRIPT);
    cmd.add("--masters");
    cmd.add(testUniverse.getMasterAddresses());
    // TODO(bogdan): we do not have nodes to test this?
    cmd.add("--ts_web_hosts_ports");
    cmd.add(testUniverse.getTserverHTTPAddresses());

    cmd.add("--parallelism");
    cmd.add("3");
    cmd.add("--ysql_port");
    cmd.add(
        Integer.toString(testUniverse.getUniverseDetails().communicationPorts.ysqlServerRpcPort));

    BackupStorageInfo backupStorageInfo = restoreParams.backupStorageInfoList.get(0);
    if (backupStorageInfo.tableNameList != null) {
      for (String tableName : backupStorageInfo.tableNameList) {
        cmd.add("--table");
        cmd.add(tableName);
      }
    }
    if (backupStorageInfo.keyspace != null) {
      cmd.add("--keyspace");
      cmd.add(backupStorageInfo.keyspace);
    }
    cmd.add("--no_auto_name");
    if (testProvider.getCode().equals("kubernetes")) {
      cmd.add("--k8s_config");
      cmd.add(Json.stringify(Json.toJson(podAddrToConfig)));
    } else {
      cmd.add("--ssh_port");
      cmd.add(accessKey.getKeyInfo().sshPort.toString());
      cmd.add("--ssh_key_path");
      cmd.add(pkPath);
      cmd.add("--ip_to_ssh_key_path");
      cmd.add(
          Json.stringify(
              Json.toJson(
                  testUniverse.getTServers().stream()
                      .collect(Collectors.toMap(t -> t.cloudInfo.private_ip, t -> pkPath)))));
    }
    cmd.add("--use_server_broadcast_address");
    cmd.add("--backup_location");
    cmd.add(backupStorageInfo.storageLocation);
    cmd.add("--storage_type");
    cmd.add(storageType);
    if (storageType.equals("nfs")) {
      cmd.add("--nfs_storage_path");
      cmd.add("/foo/bar");
    }
    if (userIntent.enableNodeToNodeEncrypt) {
      cmd.add("--certs_dir");
      cmd.add(testProvider.getCode().equals("kubernetes") ? K8S_CERT_PATH : VM_CERT_DIR);
    }
    cmd.add(actionType.name().toLowerCase());
    boolean verboseLogsEnabled =
        runtimeConfigFactory.forUniverse(testUniverse).getBoolean("yb.backup.log.verbose");
    if (restoreParams.enableVerboseLogs || verboseLogsEnabled) {
      cmd.add("--verbose");
    }
    return cmd;
  }

  private BackupTableParams getBackupUniverseParams(UUID storageUUID) {
    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.tableUUID = UUID.randomUUID();
    backupTableParams.storageConfigUUID = storageUUID;
    backupTableParams.setUniverseUUID(testUniverse.getUniverseUUID());
    List<BackupTableParams> backupList = new ArrayList<>();
    BackupTableParams b1Params = new BackupTableParams();
    b1Params.setTableName("mock_table");
    b1Params.setKeyspace("mock_ks");
    b1Params.setUniverseUUID(testUniverse.getUniverseUUID());
    b1Params.storageConfigUUID = storageUUID;
    backupList.add(b1Params);
    BackupTableParams b2Params = new BackupTableParams();
    b2Params.setKeyspace("mock_ysql");
    b2Params.setUniverseUUID(testUniverse.getUniverseUUID());
    b2Params.storageConfigUUID = storageUUID;
    backupList.add(b2Params);
    backupTableParams.backupList = backupList;
    return backupTableParams;
  }
}
