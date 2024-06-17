// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.ArrayList;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RestoreTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Provider defaultProvider;
  private CertificateHelper certificateHelper;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    defaultUniverse = createUniverse(defaultCustomer.getId());
  }

  @Test
  public void testRestoreWithUniverseInfoInBackupLocation() {
    Universe secondUniverse = createUniverse("universe-2", defaultCustomer.getId());
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST105");
    UUID taskUUID = UUID.randomUUID();

    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupTableParams.customerUuid = defaultCustomer.getUuid();
    Backup defaultBackup = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();

    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    restoreBackupParams.setUniverseUUID(secondUniverse.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);

    UUID restoreTaskUUID = UUID.randomUUID();
    Restore restore = Restore.create(restoreTaskUUID, restoreBackupParams);
    restore.setState(Restore.State.Created);
    restore.save();

    assertEquals(defaultUniverse.getUniverseUUID(), restore.getSourceUniverseUUID());
  }

  @Test
  public void testRestoreWithUniverseInfoInBackupLocationButUniverseAbsent() {
    Universe secondUniverse = createUniverse("universe-2", defaultCustomer.getId());
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST105");
    UUID taskUUID = UUID.randomUUID();
    UUID defaultUniverseUUID = defaultUniverse.getUniverseUUID();

    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupTableParams.customerUuid = defaultCustomer.getUuid();
    Backup defaultBackup = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();

    defaultUniverse.delete();
    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    restoreBackupParams.setUniverseUUID(secondUniverse.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);

    UUID restoreTaskUUID = UUID.randomUUID();
    Restore restore = Restore.create(restoreTaskUUID, restoreBackupParams);
    restore.setState(Restore.State.Created);
    restore.save();

    assertEquals(defaultUniverseUUID, restore.getSourceUniverseUUID());
  }

  @Test
  public void testRestoreWithNoUniverseInfoInBackupLocation() {
    Universe secondUniverse = createUniverse("universe-2", defaultCustomer.getId());
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST105");
    UUID taskUUID = UUID.randomUUID();
    UUID defaultUniverseUUID = defaultUniverse.getUniverseUUID();

    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupTableParams.customerUuid = defaultCustomer.getUuid();
    Backup defaultBackup = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();

    defaultUniverse.delete();
    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    restoreBackupParams.setUniverseUUID(secondUniverse.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.storageLocation = storageInfo.storageLocation.split("/", 5)[4];
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);

    UUID restoreTaskUUID = UUID.randomUUID();
    Restore restore = Restore.create(restoreTaskUUID, restoreBackupParams);
    restore.setState(Restore.State.Created);
    restore.save();

    assertNull(restore.getSourceUniverseUUID());
  }
}
