// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.DevopsBase.PY_WRAPPER;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.K8S_CERT_PATH;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.VM_CERT_DIR;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
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
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
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
public class TableManagerTest extends FakeDBApplication {

  @Mock Config mockAppConfig;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock ReleaseManager releaseManager;

  @InjectMocks TableManager tableManager;

  @Mock RuntimeConfigFactory mockruntimeConfigFactory;
  @Mock Config mockConfigUniverseScope;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock ReleasesUtils mockReleasesUtils;

  private Provider testProvider;
  private Customer testCustomer;
  private Universe testUniverse;
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
    userIntent.provider = testProvider.getUuid().toString();
    userIntent.numNodes = 3;
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

  private BulkImportParams getBulkImportParams() {
    BulkImportParams bulkImportParams = new BulkImportParams();
    bulkImportParams.setTableName("mock_table");
    bulkImportParams.setKeyspace("mock_ks");
    bulkImportParams.s3Bucket = "s3://foo.bar.com/bulkload";
    bulkImportParams.setUniverseUUID(testUniverse.getUniverseUUID());
    return bulkImportParams;
  }

  private BackupTableParams getBackupTableParams(BackupTableParams.ActionType actionType) {
    BackupTableParams backupTableParams = new BackupTableParams();
    if (actionType.equals(BackupTableParams.ActionType.CREATE)) {
      backupTableParams.tableUUID = UUID.randomUUID();
    }
    backupTableParams.setTableName("mock_table");
    backupTableParams.setKeyspace("mock_ks");
    backupTableParams.actionType = actionType;
    backupTableParams.setUniverseUUID(testUniverse.getUniverseUUID());
    return backupTableParams;
  }

  private BackupTableParams getBackupUniverseParams(
      BackupTableParams.ActionType actionType, UUID storageUUID) {
    BackupTableParams backupTableParams = new BackupTableParams();
    if (actionType.equals(BackupTableParams.ActionType.CREATE)) {
      backupTableParams.tableUUID = UUID.randomUUID();
    }
    backupTableParams.actionType = actionType;
    backupTableParams.storageConfigUUID = storageUUID;
    backupTableParams.setUniverseUUID(testUniverse.getUniverseUUID());
    List<BackupTableParams> backupList = new ArrayList<>();
    BackupTableParams b1Params = new BackupTableParams();
    b1Params.setTableName("mock_table");
    b1Params.setKeyspace("mock_ks");
    b1Params.actionType = actionType;
    b1Params.setUniverseUUID(testUniverse.getUniverseUUID());
    b1Params.storageConfigUUID = storageUUID;
    backupList.add(b1Params);
    BackupTableParams b2Params = new BackupTableParams();
    b2Params.setKeyspace("mock_ysql");
    b2Params.actionType = actionType;
    b2Params.setUniverseUUID(testUniverse.getUniverseUUID());
    b2Params.storageConfigUUID = storageUUID;
    backupList.add(b2Params);
    backupTableParams.backupList = backupList;
    return backupTableParams;
  }

  private List<String> getExpectedBulkImportCommmand(BulkImportParams bulkImportParams) {
    List<String> cmd = new LinkedList<>();
    // bin/py_wrapper bin/yb_bulk_load.py, --key_path, /path/to/private.key, --instance_count, 24,
    // --universe, Universe-1, --release, /yb/release.tar.gz,
    // --masters, host-n1:7100,host-n2:7100,host-n3:7100, --table, mock_table, --keyspace, mock_ks,
    // --s3bucket, s3://foo.bar.com/bulkload
    cmd.add(PY_WRAPPER);
    cmd.add(BULK_IMPORT.getScript());
    cmd.add("--masters");
    cmd.add(testUniverse.getMasterAddresses());
    cmd.add("--table");
    cmd.add(bulkImportParams.getTableName());
    cmd.add("--keyspace");
    cmd.add(bulkImportParams.getKeyspace());
    cmd.add("--key_path");
    cmd.add(pkPath);
    cmd.add("--instance_count");
    if (bulkImportParams.instanceCount == 0) {
      cmd.add(
          Integer.toString(
              testUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes * 8));
    } else {
      cmd.add(Integer.toString(bulkImportParams.instanceCount));
    }
    cmd.add("--universe");
    cmd.add("yb-1-" + testUniverse.getName());
    cmd.add("--release");
    cmd.add("/yb/release.tar.gz");
    cmd.add("--s3bucket");
    cmd.add(bulkImportParams.s3Bucket);
    return cmd;
  }

  private List<String> getExpectedBackupTableCommand(
      BackupTableParams backupTableParams, String storageType) {
    return getExpectedBackupTableCommand(backupTableParams, storageType, false);
  }

  private List<String> getExpectedBackupTableCommand(
      BackupTableParams backupTableParams, String storageType, boolean isDelete) {
    AccessKey accessKey = AccessKey.get(testProvider.getUuid(), keyCode);
    Map<String, Map<String, String>> podAddrToConfig = new HashMap<>();
    UserIntent userIntent = testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;

    if (testProvider.getCode().equals("kubernetes")) {
      PlacementInfo pi = testUniverse.getUniverseDetails().getPrimaryCluster().placementInfo;
      for (Cluster cluster : testUniverse.getUniverseDetails().clusters) {
        podAddrToConfig.putAll(
            KubernetesUtil.getKubernetesConfigPerPod(
                pi, testUniverse.getUniverseDetails().getNodesInCluster(cluster.uuid)));
      }
    }

    List<String> cmd = new LinkedList<>();
    cmd.add(PY_WRAPPER);
    cmd.add(BACKUP.getScript());
    cmd.add("--masters");
    cmd.add(testUniverse.getMasterAddresses());
    // TODO(bogdan): we do not have nodes to test this?
    cmd.add("--ts_web_hosts_ports");
    cmd.add(testUniverse.getTserverHTTPAddresses());

    if (!isDelete) {
      cmd.add("--parallelism");
      cmd.add("8");
      cmd.add("--ysql_port");
      cmd.add(
          Integer.toString(testUniverse.getUniverseDetails().communicationPorts.ysqlServerRpcPort));

      if (backupTableParams.tableNameList != null) {
        for (String tableName : backupTableParams.tableNameList) {
          cmd.add("--table");
          cmd.add(tableName);
          cmd.add("--keyspace");
          cmd.add(backupTableParams.getKeyspace());
        }
      } else {
        if (backupTableParams.getTableName() != null) {
          cmd.add("--table");
          cmd.add(backupTableParams.getTableName());
        }
        cmd.add("--keyspace");
        cmd.add(backupTableParams.getKeyspace());
      }
      if (backupTableParams.actionType.equals(BackupTableParams.ActionType.CREATE)
          && backupTableParams.tableUUID != null) {
        cmd.add("--table_uuid");
        cmd.add(backupTableParams.tableUUID.toString().replace("-", ""));
      }
      cmd.add("--no_auto_name");
    }
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
    cmd.add(backupTableParams.storageLocation);
    cmd.add("--storage_type");
    cmd.add(storageType);
    if (storageType.equals("nfs")) {
      cmd.add("--nfs_storage_path");
      cmd.add("/foo/bar");
    }
    if (userIntent.enableNodeToNodeEncrypt) {
      cmd.add("--certs_dir");
      cmd.add(
          testProvider.getCode().equals("kubernetes")
              ? K8S_CERT_PATH
              : testProvider.getYbHome() + VM_CERT_DIR);
    }
    cmd.add(backupTableParams.actionType.name().toLowerCase());
    boolean verboseLogsEnabled =
        mockruntimeConfigFactory.forUniverse(testUniverse).getBoolean("yb.backup.log.verbose");
    if (backupTableParams.enableVerboseLogs || verboseLogsEnabled) {
      cmd.add("--verbose");
    }
    return cmd;
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = createUniverse("Universe-1", testCustomer.getId());
    ReleaseManager.ReleaseMetadata metadata = new ReleaseManager.ReleaseMetadata();
    ReleaseContainer release =
        new ReleaseContainer(metadata, mockCloudUtilFactory, mockAppConfig, mockReleasesUtils);
    metadata.filePath = "/yb/release.tar.gz";
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn(release);
    when(mockruntimeConfigFactory.forUniverse(any())).thenReturn(mockConfigUniverseScope);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.pgBasedBackup)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.backupLogVerbose)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.enableSSE)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.useServerBroadcastAddressForYbBackup)))
        .thenReturn(true);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.disableXxHashChecksum))).thenReturn(false);
  }

  @Test
  public void testBulkImportWithDefaultInstanceCount() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    BulkImportParams bulkImportParams = getBulkImportParams();
    UserIntent userIntent = testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    List<String> expectedCommand = getExpectedBulkImportCommmand(bulkImportParams);
    Map<String, String> expectedEnvVars = CloudInfoInterface.fetchEnvVars(testProvider);
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).getCode());

    tableManager.bulkImport(bulkImportParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testBulkImportWithSpecificInstanceCount() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    BulkImportParams bulkImportParams = getBulkImportParams();
    bulkImportParams.instanceCount = 5;
    UserIntent userIntent = testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    List<String> expectedCommand = getExpectedBulkImportCommmand(bulkImportParams);
    Map<String, String> expectedEnvVars = CloudInfoInterface.fetchEnvVars(testProvider);
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).getCode());

    tableManager.bulkImport(bulkImportParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  private void testCreateS3BackupHelper(boolean enableVerbose, boolean sse) {
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST101");
    ;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupTableParams.sse = sse;
    if (enableVerbose) {
      backupTableParams.enableVerboseLogs = true;
    }
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  private void testCreateBackupKubernetesHelper() {
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG", "foo");
    testProvider.setConfigMap(config);
    testProvider.save();
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST102");
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();

    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    expectedEnvVars.put("KUBECONFIG", "foo");
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testCreateS3Backup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    testCreateS3BackupHelper(false, false);
  }

  @Test
  public void testCreateS3BackupWithSSE() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    testCreateS3BackupHelper(false, true);
  }

  @Test
  public void testCreateS3BackupVerbose() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    testCreateS3BackupHelper(true, false);
  }

  @Test
  public void testCreateNfsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST35");
    ;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "nfs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testCreateGcsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createGcsStorageConfig(testCustomer, "TEST50");
    ;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "gcs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testCreateUniverseBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST36");
    BackupTableParams backupTableParams =
        getBackupUniverseParams(BackupTableParams.ActionType.CREATE, storageConfig.getConfigUUID());
    Backup.create(testCustomer.getUuid(), backupTableParams);
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    for (BackupTableParams params : backupTableParams.backupList) {
      tableManager.createBackup(params);
      List<String> expectedCommand = getExpectedBackupTableCommand(params, "nfs");
      verify(shellProcessHandler, times(1))
          .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
    }
  }

  @Test
  public void testRestoreS3Backup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST103");
    ;
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testRestoreNfsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST37");
    ;
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "nfs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testRestoreGcsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createGcsStorageConfig(testCustomer, "TEST51");
    ;
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    Backup.create(testCustomer.getUuid(), backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "gcs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testRestoreUniverseBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST39");
    ;
    BackupTableParams backupTableParams =
        getBackupUniverseParams(
            BackupTableParams.ActionType.RESTORE, storageConfig.getConfigUUID());
    Backup.create(testCustomer.getUuid(), backupTableParams);
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    for (BackupTableParams params : backupTableParams.backupList) {
      tableManager.createBackup(params);
      List<String> expectedCommand = getExpectedBackupTableCommand(params, "nfs");
      verify(shellProcessHandler, times(1))
          .run(expectedCommand, expectedEnvVars, params.backupUuid);
    }
  }

  @Test
  public void testRestoreS3BackupWithRestoreTimeStamp() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST41");
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    Date date = new Date();
    backupTableParams.restoreTimeStamp = formatter.format(date);
    Backup.create(testCustomer.getUuid(), backupTableParams);
    try {
      tableManager.createBackup(backupTableParams);
    } catch (Exception e) {
      assertNull(e);
    }
  }

  @Test
  public void testRestoreNfsBackupWithRestoreTimeStamp() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST42");
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    Date date = new Date();
    backupTableParams.restoreTimeStamp = formatter.format(date);
    Backup.create(testCustomer.getUuid(), backupTableParams);
    try {
      tableManager.createBackup(backupTableParams);
    } catch (Exception e) {
      assertNull(e);
    }
  }

  @Test
  public void testRestoreGcsBackupWithRestoreTimeStamp() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createGcsStorageConfig(testCustomer, "TEST43");
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    Date date = new Date();
    backupTableParams.restoreTimeStamp = formatter.format(date);
    Backup.create(testCustomer.getUuid(), backupTableParams);
    try {
      tableManager.createBackup(backupTableParams);
    } catch (Exception e) {
      assertNull(e);
    }
  }

  @Test
  public void testRestoreBackupWithInvalidTimeStamp() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST44");
    BackupTableParams backupTableParams =
        getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();
    SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    Date date = new Date();
    backupTableParams.restoreTimeStamp = formatter.format(date);
    Backup.create(testCustomer.getUuid(), backupTableParams);
    try {
      tableManager.createBackup(backupTableParams);
    } catch (Exception e) {
      assertEquals(
          "Invalid restore timeStamp format, Please provide it in yyyy-MM-dd HH:mm:ss format",
          e.getMessage());
    }
  }

  @Test
  public void testCreateBackupWithSSHUser() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    AccessKey accessKey = AccessKey.get(testProvider.getUuid(), keyCode);
    AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
    keyInfo.sshUser = "foo";
    accessKey.setKeyInfo(keyInfo);
    accessKey.save();

    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer, "TEST104");
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.getConfigUUID();

    Backup.create(testCustomer.getUuid(), backupTableParams);
    // Backups should always be done as the yugabyte user.
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1))
        .run(expectedCommand, expectedEnvVars, backupTableParams.backupUuid);
  }

  @Test
  public void testBulkImportWithIncorrectYBVersion() {
    setupUniverse(ModelFactory.awsProvider(testCustomer), "0.0.2");
    BulkImportParams bulkImportParams = getBulkImportParams();
    try {
      tableManager.bulkImport(bulkImportParams);
    } catch (RuntimeException re) {
      assertEquals("Unable to fetch yugabyte release for version: 0.0.2", re.getMessage());
    }
  }

  @Test
  public void testCreateTLSBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer), "0.0.1", true);
    testCreateS3BackupHelper(false, false);
  }

  @Test
  public void testCreateBackupKubernetes() {
    setupUniverse(ModelFactory.kubernetesProvider(testCustomer));
    testCreateBackupKubernetesHelper();
  }

  @Test
  public void testCreateBackupKubernetesWithTLS() {
    setupUniverse(ModelFactory.kubernetesProvider(testCustomer), "0.0.1", true);
    testCreateBackupKubernetesHelper();
  }

  @Test
  public void testDeleteUniverseBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer, "TEST40");
    BackupTableParams backupTableParams =
        getBackupUniverseParams(BackupTableParams.ActionType.CREATE, storageConfig.getConfigUUID());
    Backup.create(testCustomer.getUuid(), backupTableParams);
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    for (BackupTableParams params : backupTableParams.backupList) {
      tableManager.deleteBackup(params);
      List<String> expectedCommand = getExpectedBackupTableCommand(params, "nfs", true);
      verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
    }
  }
}
