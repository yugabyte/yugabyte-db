// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DestroyUniverseTest extends CommissionerBaseTest {

  private CustomerConfig s3StorageConfig;

  private MetricService metricService;

  private Universe defaultUniverse;

  private CertificateInfo certInfo;

  private File certFolder;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.uuid);

    String certificate = "certificates/ca.crt";
    File certFile = new File(certificate);
    certFile.getParentFile().mkdirs();
    try {
      certFile.createNewFile();
      certFolder = certFile.getParentFile();
      certInfo =
          ModelFactory.createCertificateInfo(
              defaultCustomer.getUuid(), certificate, CertificateInfo.Type.SelfSigned);
    } catch (Exception e) {

    }

    metricService = app.injector().instanceOf(MetricService.class);

    defaultUniverse = createUniverse(defaultCustomer.getCustomerId(), certInfo.uuid);
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));

    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
  }

  @After
  public void tearDown() {
    if (certFolder.exists()) {
      certFolder.delete();
    }
  }

  @Test
  public void testReleaseUniverseAndRemoveMetrics() {
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;

    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, defaultUniverse));

    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));

    MetricKey metricKey =
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .targetUuid(defaultUniverse.getUniverseUUID())
            .build();
    assertThat(metricService.get(metricKey), nullValue());
  }

  @Test
  public void testDestroyUniverseAndDeleteBackups() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.TRUE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());

    Backup backup = Backup.get(defaultCustomer.uuid, b.backupUUID);
    verify(mockTableManager, times(1)).deleteBackup(any());
    // Backup state should be DELETED.
    assertEquals(Backup.BackupState.Deleted, backup.state);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
  }

  @Test
  public void testDestroyUniverseAndDeleteBackupsFalse() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    b.setTaskUUID(taskInfo.getTaskUUID());

    Backup backup = Backup.get(defaultCustomer.uuid, b.backupUUID);
    verify(mockTableManager, times(0)).deleteBackup(any());
    // Backup should be in COMPLETED state.
    assertEquals(Backup.BackupState.Completed, backup.state);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
  }

  @Test
  public void testDestroyUniverseAndDeleteAssociatedCerts() {
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.TRUE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
    assertFalse(certFolder.exists());
    assertNull(CertificateInfo.get(certInfo.uuid));
  }

  private TaskInfo submitTask(DestroyUniverse.Params taskParams, int version) {
    taskParams.expectedUniverseVersion = version;
    try {
      UUID taskUUID = commissioner.submit(TaskType.DestroyUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testDestroyUniverseRestoredFromAnotherUniverseBackup() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    Backup b =
        ModelFactory.restoreBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.TRUE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    b.setTaskUUID(taskInfo.getTaskUUID());

    Backup backup = Backup.get(defaultCustomer.uuid, b.backupUUID);
    verify(mockTableManager, times(0)).deleteBackup(any());
    // Backup should be in COMPLETED state.
    assertEquals(Backup.BackupState.Completed, backup.state);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
  }
}
