package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

public class CreateBackupScheduleKubernetesTest extends CommissionerBaseTest {
  private Universe defaultUniverse;
  private CustomerConfig storageConfig;
  private Users defaultUser;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createK8sUniverseCustomCores(
            "test-universe", UUID.randomUUID(), defaultCustomer.getId(), null, null, true, 2.0);
    Universe.UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams params = universe.getUniverseDetails();
            params.setYbcInstalled(true);
            params.setEnableYbc(true);
          }
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 3);
    defaultUser = ModelFactory.testUser(defaultCustomer);
    storageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "S3-config");
  }

  private TaskInfo submitTask(BackupRequestParams scheduleParams) {
    try {
      BackupScheduleTaskParams params =
          UniverseControllerRequestBinder.deepCopy(
              defaultUniverse.getUniverseDetails(), BackupScheduleTaskParams.class);
      params.scheduleParams = scheduleParams;
      params.customerUUID = defaultCustomer.getUuid();
      UUID taskUUID = commissioner.submit(TaskType.CreateBackupScheduleKubernetes, params);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Schedule,
          CustomerTask.TaskType.Create,
          defaultUniverse.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private BackupRequestParams createScheduleBackupParams(
      long frequency,
      TimeUnit frequencyTimeUnit,
      long incrementalBackupFrequency,
      TimeUnit incrementalBackupFrequencyTimeUnit,
      String cron,
      boolean enablePITRestore,
      String scheduleName) {
    BackupRequestParams backupParams = new BackupRequestParams();
    backupParams.schedulingFrequency = frequency;
    backupParams.frequencyTimeUnit = frequencyTimeUnit;
    backupParams.incrementalBackupFrequency = incrementalBackupFrequency;
    backupParams.incrementalBackupFrequencyTimeUnit = incrementalBackupFrequencyTimeUnit;
    backupParams.cronExpression = cron;
    backupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupParams.enablePointInTimeRestore = enablePITRestore;
    backupParams.scheduleName = scheduleName;
    backupParams.backupType = TableType.PGSQL_TABLE_TYPE;
    return backupParams;
  }

  @Test
  public void testCreateScheduleBackupAsyncFailOnDiskResizePlacementModification() {
    BackupRequestParams params =
        createScheduleBackupParams(10000000L, TimeUnit.HOURS, 0L, null, null, true, "test");
    UUID placementTaskUUID = UUID.randomUUID();
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams params = universe.getUniverseDetails();
            params.clusters.get(0).userIntent.ybSoftwareVersion = "2.23.0.0-b540";
            params.clusters.get(0).userIntent.deviceInfo = new DeviceInfo();
            params.clusters.get(0).userIntent.deviceInfo.volumeSize = 100;
            params.placementModificationTaskUuid = placementTaskUUID;
            universe.setUniverseDetails(params);
          }
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    // Update volume size in task
    UniverseDefinitionTaskParams taskParams = defaultUniverse.getUniverseDetails();
    taskParams.clusters.get(0).userIntent.deviceInfo = new DeviceInfo();
    taskParams.clusters.get(0).userIntent.deviceInfo.volumeSize = 110;
    TaskInfo tInfo = new TaskInfo(TaskType.EditKubernetesUniverse, placementTaskUUID);
    tInfo.setOwner("test");
    tInfo.setTaskParams(Json.toJson(taskParams));
    tInfo.save();

    RuntimeException ex = assertThrows(RuntimeException.class, () -> submitTask(params));
    assertEquals(
        "Universe "
            + defaultUniverse.getUniverseUUID().toString()
            + " cannot run restricted CreateBackupScheduleKubernetes task",
        ex.getMessage());
  }

  @Test
  public void testCreateScheduleBackupAsyncFailOnSoftwareVersion() {
    BackupRequestParams params =
        createScheduleBackupParams(10000000L, TimeUnit.HOURS, 0L, null, null, true, "test");
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams params = universe.getUniverseDetails();
            params.clusters.get(0).userIntent.ybSoftwareVersion = "2.23.0.0-b529";
            universe.setUniverseDetails(params);
          }
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    RuntimeException ex = assertThrows(RuntimeException.class, () -> submitTask(params));
    assertEquals(
        "Software version 2.23.0.0-b529 does not support Point In Time Recovery enabled backup"
            + " schedules",
        ex.getMessage());
  }
}
