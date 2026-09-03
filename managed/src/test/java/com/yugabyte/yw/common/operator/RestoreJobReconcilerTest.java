// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
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
import java.util.UUID;
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

  private RestoreJob createRestoreJobCr() {
    RestoreJob restoreJob = new RestoreJob();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("test-restore-job");
    metadata.setNamespace(NAMESPACE);
    restoreJob.setMetadata(metadata);

    RestoreJobSpec spec = new RestoreJobSpec();
    spec.setBackup("test-backup-cr");
    spec.setKeyspace("test-keyspace");
    spec.setUniverse("test-universe");
    restoreJob.setSpec(spec);
    return restoreJob;
  }

  @Test
  public void testGetRestoreParamsPopulatesCommonFields() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    RestoreJob restoreJob = createRestoreJobCr();
    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertEquals(testCustomer.getUuid(), params.customerUUID);
    assertEquals(testUniverse.getUniverseUUID(), params.getUniverseUUID());
    assertEquals(testStorageConfig.getConfigUUID(), params.storageConfigUUID);
  }

  @Test
  public void testGetRestoreParamsResolvesKmsConfigWhenSet() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    UUID resolvedKmsConfigUUID = UUID.randomUUID();
    doReturn(resolvedKmsConfigUUID)
        .when(operatorUtils)
        .resolveReadyKmsConfigUuid(eq("test-kms-config-cr"), eq(NAMESPACE));

    RestoreJob restoreJob = createRestoreJobCr();
    restoreJob.getSpec().setKmsConfig("test-kms-config-cr");

    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    // The referenced KMSConfig CR is resolved to its YBA config UUID and plumbed into the params.
    assertEquals(resolvedKmsConfigUUID, params.kmsConfigUUID);
    verify(operatorUtils).resolveReadyKmsConfigUuid(eq("test-kms-config-cr"), eq(NAMESPACE));
  }

  @Test
  public void testGetRestoreParamsLeavesKmsConfigNullWhenUnset() throws Exception {
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    io.yugabyte.operator.v1alpha1.Backup backupCr = createBackupCr(testBackup);
    when(backupIndexer.list()).thenReturn(Collections.singletonList(backupCr));

    // kmsConfig left unset on the spec (plaintext backup case).
    RestoreJob restoreJob = createRestoreJobCr();

    RestoreBackupParams params = restoreJobReconciler.getRestoreBackupParamsFromCr(restoreJob);

    assertNull(params.kmsConfigUUID);
    verify(operatorUtils, never()).resolveReadyKmsConfigUuid(anyString(), anyString());
  }
}
