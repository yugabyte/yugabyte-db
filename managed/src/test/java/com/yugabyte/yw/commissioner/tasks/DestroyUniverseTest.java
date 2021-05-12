// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.createBackup;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import java.util.List;
import java.util.UUID;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;

@RunWith(MockitoJUnitRunner.class)
public class DestroyUniverseTest extends CommissionerBaseTest {

  private static final String ALERT_TEST_MESSAGE = "Test message";
  private CustomerConfig s3StorageConfig;
  
  @InjectMocks
  private Commissioner commissioner;

  private Universe defaultUniverse;
  private ShellResponse dummyShellResponse;

  @Before
  public void setUp() {
    super.setUp();
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));

    dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
  }

  @Test
  public void testReleaseUniverseAndResolveAlerts() {
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;

    Alert.create(defaultCustomer.uuid, defaultUniverse.universeUUID, Alert.TargetType.UniverseType,
        "errorCode", "Warning", ALERT_TEST_MESSAGE);
    Alert.create(defaultCustomer.uuid, defaultUniverse.universeUUID, Alert.TargetType.UniverseType,
        "errorCode2", "Warning", ALERT_TEST_MESSAGE);

    submitTask(taskParams, 4);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));

    List<Alert> alerts = Alert.list(defaultCustomer.uuid);
    assertEquals(2, alerts.size());
    assertEquals(Alert.State.RESOLVED, alerts.get(0).state);
    assertEquals(Alert.State.RESOLVED, alerts.get(1).state);
  }

  @Test
  public void testDestroyUniverseAndDeleteBackups() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer);
    Backup b = ModelFactory.createBackup(defaultCustomer.uuid, defaultUniverse.universeUUID,
        s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    ShellResponse shellResponse =  new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.TRUE;
    TaskInfo taskInfo = submitTask(taskParams, 4);

    Backup backup = Backup.get(defaultCustomer.uuid, b.backupUUID);
    verify(mockTableManager, times(1)).deleteBackup(any());
    // Backup state should be DELETED.
    assertEquals(Backup.BackupState.Deleted, backup.state);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
  }

  @Test
  public void testDestroyUniverseAndDeleteBackupsFalse() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer);
    Backup b = ModelFactory.createBackup(defaultCustomer.uuid, defaultUniverse.universeUUID,
        s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    b.setTaskUUID(taskInfo.getTaskUUID());

    Backup backup = Backup.get(defaultCustomer.uuid, b.backupUUID);
    verify(mockTableManager, times(0)).deleteBackup(any());
    // Backup should be in COMPLETED state.
    assertEquals(Backup.BackupState.Completed, backup.state);
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.name));
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
}
