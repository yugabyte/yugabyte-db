// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.TableManager.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.TableManager.PY_WRAPPER;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
  public class TableManagerTest extends FakeDBApplication {

  @Mock
  play.Configuration mockAppConfig;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  ReleaseManager releaseManager;

  @InjectMocks
  TableManager tableManager;

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
      AvailabilityZone.create(r, azCode, azName, subnetName);
      regionUUIDs.add(r.uuid);
    }
    return regionUUIDs;
  }

  private void setupUniverse(Provider p) {
    testProvider = p;
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = pkPath;
    if (AccessKey.get(testProvider.uuid, keyCode) == null) {
      AccessKey.create(testProvider.uuid, keyCode, keyInfo);
    }

    UniverseDefinitionTaskParams uniParams = new UniverseDefinitionTaskParams();
    uniParams.nodePrefix = "yb-1-" + testUniverse.name;
    UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.accessKeyCode = keyCode;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.regionList = getMockRegionUUIDs(3);
    uniParams.upsertPrimaryCluster(userIntent, null);
    testUniverse.setUniverseDetails(uniParams);
    testUniverse = Universe.saveDetails(testUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, uniParams.nodePrefix));
  }

  private BulkImportParams getBulkImportParams() {
    BulkImportParams bulkImportParams = new BulkImportParams();
    bulkImportParams.tableName = "mock_table";
    bulkImportParams.keyspace = "mock_ks";
    bulkImportParams.s3Bucket = "s3://foo.bar.com/bulkload";
    bulkImportParams.universeUUID = testUniverse.universeUUID;
    return bulkImportParams;
  }

  private BackupTableParams getBackupTableParams(BackupTableParams.ActionType actionType) {
    BackupTableParams backupTableParams = new BackupTableParams();
    if (actionType.equals(BackupTableParams.ActionType.CREATE)) {
      backupTableParams.tableUUID = UUID.randomUUID();
    }
    backupTableParams.tableName = "mock_table";
    backupTableParams.keyspace = "mock_ks";
    backupTableParams.actionType = actionType;
    backupTableParams.universeUUID = testUniverse.universeUUID;
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
    cmd.add(bulkImportParams.tableName);
    cmd.add("--keyspace");
    cmd.add(bulkImportParams.keyspace);
    cmd.add("--key_path");
    cmd.add(pkPath);
    cmd.add("--instance_count");
    if (bulkImportParams.instanceCount == 0) {
      cmd.add(Integer.toString(testUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes * 8));
    } else {
      cmd.add(Integer.toString(bulkImportParams.instanceCount));
    }
    cmd.add("--universe");
    cmd.add("yb-1-" + testUniverse.name);
    cmd.add("--release");
    cmd.add("/yb/release.tar.gz");
    cmd.add("--s3bucket");
    cmd.add(bulkImportParams.s3Bucket);
    return cmd;
  }

  private List<String> getExpectedBackupTableCommand(
      BackupTableParams backupTableParams, String storageType) {
    AccessKey accessKey = AccessKey.get(testProvider.uuid, keyCode);

    List<String> cmd = new LinkedList<>();
    cmd.add(PY_WRAPPER);
    cmd.add(BACKUP.getScript());
    cmd.add("--masters");
    // TODO(bogdan): we do not have nodes to test this?
    if (testProvider.code.equals("kubernetes")) {
      cmd.add(testUniverse.getKubernetesMasterAddresses());
    } else {
      cmd.add(testUniverse.getMasterAddresses());
    }
    cmd.add("--table");
    cmd.add(backupTableParams.tableName);
    cmd.add("--keyspace");
    cmd.add(backupTableParams.keyspace);
    if (testProvider.code.equals("kubernetes")) {
      cmd.add("--k8s_namespace");
      cmd.add(testUniverse.getUniverseDetails().nodePrefix);
    } else {
      if (accessKey.getKeyInfo().sshUser != null) {
        cmd.add("--ssh_user");
        cmd.add(accessKey.getKeyInfo().sshUser);
      }
      cmd.add("--ssh_key_path");
      cmd.add(pkPath);
    }
    cmd.add("--s3bucket");
    cmd.add(backupTableParams.storageLocation);
    cmd.add("--storage_type");
    cmd.add(storageType);
    if (backupTableParams.actionType.equals(BackupTableParams.ActionType.CREATE)) {
      cmd.add("--table_uuid");
      cmd.add(backupTableParams.tableUUID.toString().replace("-", ""));
    }
    cmd.add("--no_auto_name");
    cmd.add(backupTableParams.actionType.name().toLowerCase());
    return cmd;
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = createUniverse("Universe-1", testCustomer.getCustomerId());
    testCustomer.addUniverseUUID(testUniverse.universeUUID);
    testCustomer.save();
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn("/yb/release.tar.gz");
  }

  @Test
  public void testBulkImportWithDefaultInstanceCount() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    BulkImportParams bulkImportParams = getBulkImportParams();
    UserIntent userIntent = testUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    List<String> expectedCommand = getExpectedBulkImportCommmand(bulkImportParams);
    Map<String, String> expectedEnvVars = testProvider.getConfig();
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).code);

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
    Map<String, String> expectedEnvVars = testProvider.getConfig();
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).code);

    tableManager.bulkImport(bulkImportParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testCreateS3Backup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer);;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;
    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testCreateNfsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer);;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;
    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "nfs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testRestoreS3Backup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer);;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;
    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testRestoreNfsBackup() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(testCustomer);;
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.RESTORE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;
    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "nfs");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testCreateBackupWithSSHUser() {
    setupUniverse(ModelFactory.awsProvider(testCustomer));
    AccessKey accessKey = AccessKey.get(testProvider.uuid, keyCode);
    AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
    keyInfo.sshUser = "foo";
    accessKey.setKeyInfo(keyInfo);
    accessKey.save();

    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer);
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;

    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testCreateBackupKubernetes() {
    setupUniverse(ModelFactory.kubernetesProvider(testCustomer));

    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(testCustomer);
    BackupTableParams backupTableParams = getBackupTableParams(BackupTableParams.ActionType.CREATE);
    backupTableParams.storageConfigUUID = storageConfig.configUUID;

    Backup.create(testCustomer.uuid, backupTableParams);
    List<String> expectedCommand = getExpectedBackupTableCommand(backupTableParams, "s3");
    Map<String, String> expectedEnvVars = storageConfig.dataAsMap();
    tableManager.createBackup(backupTableParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }
}
