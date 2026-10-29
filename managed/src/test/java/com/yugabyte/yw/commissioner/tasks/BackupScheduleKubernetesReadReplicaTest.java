// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.upgrade.KubernetesUpgradeTaskTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;

public class BackupScheduleKubernetesReadReplicaTest extends KubernetesUpgradeTaskTest {

  private CustomerConfig storageConfig;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    setupUniverseSingleAZ(true /* setMasters */, true /* mockGetLeaderMaster */);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> u.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL = true);
    useKubernetesNodeNames();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.25.0.0-b1";
              details.getPrimaryCluster().userIntent.setUseYbdbInbuiltYbc(true);
              details.setYbcInstalled(true);
              details.setEnableYbc(true);
              u.setUniverseDetails(details);
            });
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "S3-config");
    try {
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    } catch (Exception ignored) {
    }
  }

  private TaskInfo submitCreateSchedule() throws InterruptedException {
    BackupRequestParams scheduleParams = new BackupRequestParams();
    scheduleParams.schedulingFrequency = 3600000L;
    scheduleParams.frequencyTimeUnit = TimeUnit.HOURS;
    scheduleParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    scheduleParams.storageConfigUUID = storageConfig.getConfigUUID();
    scheduleParams.enablePointInTimeRestore = true;
    scheduleParams.scheduleName = "pit-schedule";
    scheduleParams.backupType = TableType.PGSQL_TABLE_TYPE;
    scheduleParams.customerUUID = defaultCustomer.getUuid();

    BackupScheduleTaskParams params =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.scheduleParams = scheduleParams;
    params.customerUUID = defaultCustomer.getUuid();
    UUID taskUUID = commissioner.submit(TaskType.CreateBackupScheduleKubernetes, params);
    TestUtils.setFakeHttpContext(ModelFactory.testUser(defaultCustomer));
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Create,
        defaultUniverse.getName());
    return waitForTask(taskUUID);
  }

  // PLAT-23391: the history retention pass over the read replica must target its own releases.
  @Test
  public void testPitScheduleTreatsReadReplicaAsReadReplica() throws InterruptedException {
    Cluster readReplica = addReadReplicaCluster();
    TaskInfo taskInfo = submitCreateSchedule();
    assertEquals(Success, taskInfo.getTaskState());
    assertReadReplicaCommandsTargetReadReplica(taskInfo, readReplica);
  }
}
