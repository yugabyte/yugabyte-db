// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.yugabyte.operator.v1alpha1.BackupStatus;
import io.yugabyte.operator.v1alpha1.RestoreJob;
import io.yugabyte.operator.v1alpha1.RestoreJobSpec;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RestoreJobReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<RestoreJob> restoreJobInformer;

  @Mock SharedIndexInformer<io.yugabyte.operator.v1alpha1.Backup> backupInformer;

  @Mock
  MixedOperation<RestoreJob, KubernetesResourceList<RestoreJob>, Resource<RestoreJob>>
      resourceClient;

  @Mock BackupHelper backupHelper;
  @Mock ValidatingFormFactory formFactory;
  @Mock Indexer<RestoreJob> restoreJobIndexer;
  @Mock Indexer<io.yugabyte.operator.v1alpha1.Backup> backupIndexer;

  private RestoreJobReconciler restoreJobReconciler;
  private OperatorUtils operatorUtils;
  private Customer testCustomer;
  private Universe testUniverse;
  private CustomerConfig testStorageConfig;
  private Backup testBackup;

  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() {
    when(restoreJobInformer.getIndexer()).thenReturn(restoreJobIndexer);
    when(backupInformer.getIndexer()).thenReturn(backupIndexer);

    RuntimeConfGetter mockConfGetter = Mockito.mock(RuntimeConfGetter.class);
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
    testStorageConfig = ModelFactory.createS3StorageConfig(testCustomer, "test-storage");

    testBackup =
        ModelFactory.createBackup(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID());
    testBackup.setState(BackupState.Completed);
    BackupTableParams backupInfo = testBackup.getBackupInfo();
    BackupTableParams entry = new BackupTableParams();
    entry.storageLocation = "s3://test-bucket/backup-location";
    backupInfo.backupList = new ArrayList<>(List.of(entry));
    testBackup.setBackupInfo(backupInfo);
    testBackup.save();

    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))
        .thenReturn(testCustomer.getUuid().toString());

    operatorUtils =
        spy(
            new OperatorUtils(
                mockConfGetter,
                mockReleaseManager,
                mockYbcManager,
                Mockito.mock(ValidatingFormFactory.class),
                Mockito.mock(YBClientService.class),
                Mockito.mock(KubernetesClientFactory.class),
                Mockito.mock(UniverseImporter.class),
                Mockito.mock(com.yugabyte.yw.common.KubernetesManagerFactory.class)));

    restoreJobReconciler =
        new RestoreJobReconciler(
            restoreJobInformer,
            backupInformer,
            resourceClient,
            backupHelper,
            formFactory,
            NAMESPACE,
            operatorUtils);
  }

  private io.yugabyte.operator.v1alpha1.Backup createBackupCr(Backup ybaBackup) {
    io.yugabyte.operator.v1alpha1.Backup backupCr = new io.yugabyte.operator.v1alpha1.Backup();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("test-backup-cr");
    metadata.setNamespace(NAMESPACE);
    backupCr.setMetadata(metadata);

    BackupStatus status = new BackupStatus();
    status.setResourceUUID(ybaBackup.getBackupUUID().toString());
    backupCr.setStatus(status);
    return backupCr;
  }

  private RestoreJob createRestoreJobCr(
      Boolean useTablespaces, Boolean useRoles, Boolean usePrivileges) {
    RestoreJob restoreJob = new RestoreJob();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("test-restore-job");
    metadata.setNamespace(NAMESPACE);
    restoreJob.setMetadata(metadata);

    RestoreJobSpec spec = new RestoreJobSpec();
    spec.setBackup("test-backup-cr");
    spec.setKeyspace("test-keyspace");
    spec.setUniverse("test-universe");
    spec.setUseTablespaces(useTablespaces);
    spec.setUseRoles(useRoles);
    spec.setUsePrivileges(usePrivileges);
    restoreJob.setSpec(spec);
    return restoreJob;
  }

  @Test
  public void testGetRestoreParamsWithExplicitValues() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    RestoreJob restoreJob = createRestoreJobCr(true, true, false);
    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertNotNull(params);
    assertNotNull(params.backupStorageInfoList);
    assertFalse(params.backupStorageInfoList.isEmpty());

    for (BackupStorageInfo bsi : params.backupStorageInfoList) {
      assertTrue("useTablespaces should be true", bsi.isUseTablespaces());
      assertEquals("useRoles should be true", true, bsi.getUseRoles());
      assertEquals("usePrivileges should be false", false, bsi.getUsePrivileges());
      assertEquals("test-keyspace", bsi.keyspace);
    }
  }

  @Test
  public void testGetRestoreParamsWithDefaults() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    RestoreJob restoreJob = createRestoreJobCr(false, false, true);
    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertNotNull(params);
    assertNotNull(params.backupStorageInfoList);
    assertFalse(params.backupStorageInfoList.isEmpty());

    for (BackupStorageInfo bsi : params.backupStorageInfoList) {
      assertFalse("useTablespaces should be false", bsi.isUseTablespaces());
      assertEquals("useRoles should be false", false, bsi.getUseRoles());
      assertEquals("usePrivileges should be true", true, bsi.getUsePrivileges());
    }
  }

  @Test
  public void testGetRestoreParamsWithNullFieldsUsesDefaults() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    // null fields should fall back to defaults: false, false, true
    RestoreJob restoreJob = createRestoreJobCr(null, null, null);
    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertNotNull(params);
    assertNotNull(params.backupStorageInfoList);
    assertFalse(params.backupStorageInfoList.isEmpty());

    for (BackupStorageInfo bsi : params.backupStorageInfoList) {
      assertFalse("useTablespaces should default to false", bsi.isUseTablespaces());
      assertEquals("useRoles should default to false", false, bsi.getUseRoles());
      assertEquals("usePrivileges should default to true", true, bsi.getUsePrivileges());
    }
  }

  @Test
  public void testGetRestoreParamsPopulatesCommonFields() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    RestoreJob restoreJob = createRestoreJobCr(true, false, true);
    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertEquals(testCustomer.getUuid(), params.customerUUID);
    assertEquals(testUniverse.getUniverseUUID(), params.getUniverseUUID());
    assertEquals(testStorageConfig.getConfigUUID(), params.storageConfigUUID);
  }
}
