// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.Backup;
import io.yugabyte.operator.v1alpha1.BackupSpec;
import io.yugabyte.operator.v1alpha1.BackupSpec.BackupType;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class BackupReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<Backup> backupInformer;
  @Mock MixedOperation<Backup, KubernetesResourceList<Backup>, Resource<Backup>> resourceClient;
  @Mock BackupHelper backupHelper;
  @Mock ValidatingFormFactory formFactory;
  @Mock SharedIndexInformer<StorageConfig> scInformer;
  @Mock OperatorUtils operatorUtils;
  @Mock Indexer<Backup> indexer;

  private BackupReconciler backupReconciler;
  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() {
    when(backupInformer.getIndexer()).thenReturn(indexer);
    backupReconciler =
        new BackupReconciler(
            backupInformer,
            resourceClient,
            backupHelper,
            formFactory,
            NAMESPACE,
            scInformer,
            operatorUtils);
  }

  private Backup createBackupCr(String name) {
    Backup backup = new Backup();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    BackupSpec spec = new BackupSpec();
    spec.setBackupType(BackupType.PGSQL_TABLE_TYPE);
    spec.setSse(false);
    backup.setSpec(spec);
    backup.setMetadata(metadata);
    backup.setStatus(null);
    return backup;
  }

  @Test
  public void testOnAddAddsResourceToTrackedResources() throws Exception {
    String backupName = "test-backup";
    Backup backup = createBackupCr(backupName);
    when(operatorUtils.getBackupRequestFromCr(any(Backup.class), eq(scInformer)))
        .thenThrow(new RuntimeException("mock - skip full processing"));

    backupReconciler.onAdd(backup);

    assertEquals(1, backupReconciler.getTrackedResources().size());
    KubernetesResourceDetails details = backupReconciler.getTrackedResources().iterator().next();
    assertEquals(backupName, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the backup name",
        allResources.get(0).getName().contains(backupName));
    Backup rBackup = Serialization.unmarshal(allResources.get(0).getData(), Backup.class);
    assertEquals(backupName, rBackup.getMetadata().getName());
    assertEquals(NAMESPACE, rBackup.getMetadata().getNamespace());
    assertEquals(BackupType.PGSQL_TABLE_TYPE, rBackup.getSpec().getBackupType());
  }

  @Test
  public void testOnDeleteRemovesOperatorResource() throws Exception {
    String backupName = "test-backup-delete";
    Backup backup = createBackupCr(backupName);
    when(operatorUtils.getBackupRequestFromCr(any(Backup.class), eq(scInformer)))
        .thenThrow(new RuntimeException("mock - skip full processing"));

    backupReconciler.onAdd(backup);
    assertEquals(1, OperatorResource.getAll().size());

    // Delete - backup has no status, so handleDelete takes the simple path
    backupReconciler.onDelete(backup, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        backupReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testOnAddWithExistingStatusDoesNotAddToTrackedResources() {
    Backup backup = createBackupCr("existing-backup");
    io.yugabyte.operator.v1alpha1.BackupStatus status =
        new io.yugabyte.operator.v1alpha1.BackupStatus();
    status.setResourceUUID(UUID.randomUUID().toString());
    backup.setStatus(status);

    backupReconciler.onAdd(backup);

    assertTrue(
        "Tracked resources should be empty when status already set (early return)",
        backupReconciler.getTrackedResources().isEmpty());

    // Verify no OperatorResource entries were created in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertTrue(
        "No OperatorResource entries should exist when early return occurs",
        allResources.isEmpty());
  }

  @Test
  public void testOnAddWithIgnoreLabelDoesNotTrackResource() {
    Backup backup = createBackupCr("schedule-owned-backup");
    backup
        .getMetadata()
        .setLabels(Collections.singletonMap(OperatorUtils.IGNORE_RECONCILER_ADD_LABEL, "true"));

    backupReconciler.onAdd(backup);

    assertTrue(
        "Tracked resources should be empty for backups with ignore label",
        backupReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "No OperatorResource entries should exist for ignored backups",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testOnAddWithYbaResourceIdAnnotationDoesNotTrackResource() {
    Backup backup = createBackupCr("already-controlled-backup");
    backup
        .getMetadata()
        .setAnnotations(
            Collections.singletonMap(
                ResourceAnnotationKeys.YBA_RESOURCE_ID, UUID.randomUUID().toString()));

    backupReconciler.onAdd(backup);

    assertTrue(
        "Tracked resources should be empty for already-controlled backups",
        backupReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "No OperatorResource entries should exist for already-controlled backups",
        OperatorResource.getAll().isEmpty());
  }
}
