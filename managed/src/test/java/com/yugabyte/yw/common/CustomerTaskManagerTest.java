// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.AZUpgradeStatus;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.ebean.DB;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerTaskManagerTest extends FakeDBApplication {
  Customer customer;
  Universe universe;
  CustomerTaskManager taskManager;
  YBClient mockClient;

  private CustomerTask createTask(
      CustomerTask.TargetType targetType, UUID targetUUID, CustomerTask.TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse, null);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.save();
    return CustomerTask.create(
        customer, targetUUID, taskInfo.getUuid(), targetType, taskType, "Foo");
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    taskManager = app.injector().instanceOf(CustomerTaskManager.class);
    when(mockCommissioner.isTaskRetryable(any(), any())).thenReturn(true);
  }

  @Test
  @Ignore
  public void testFailPendingTasksNoneExist() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      taskInfo.save();
      th.markAsCompleted();
    }

    taskManager.handleAllPendingTasks();
    // failPendingTask should never be called since all tasks are already completed
    verify(taskManager, times(0)).handlePendingTask(any(), any());
  }

  @Test
  @Ignore
  public void testHandlePendingTasksForCompletedCustomerTask() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      // CustomerTask is marked completed, but TaskInfo is still in Create state.
      th.markAsCompleted();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  @Ignore
  public void testFailPendingTasksForRunningTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Running);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    }
  }

  @Test
  @Ignore
  public void testFailPendingTasksForCompletedTaskInfo() throws Exception {
    universe = ModelFactory.createUniverse(customer.getId());
    mockClient = mock(YBClient.class);
    for (CustomerTask.TargetType targetType : CustomerTask.TargetType.values()) {
      UUID targetUUID = UUID.randomUUID();
      if (targetType.equals(CustomerTask.TargetType.Universe))
        targetUUID = universe.getUniverseUUID();
      CustomerTask th = createTask(targetType, targetUUID, Create);
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(th.getTaskUUID());
      taskInfo.setTaskState(TaskInfo.State.Success);
      // CustomerTask is NOT marked completed, but TaskInfo is Running state.
      taskInfo.save();
    }

    taskManager.handleAllPendingTasks();
    verify(taskManager, times(CustomerTask.TargetType.values().length))
        .handlePendingTask(any(), any());

    List<CustomerTask> customerTasks =
        CustomerTask.find.query().where().eq("customer_uuid", customer.getUuid()).findList();

    // Verify tasks have been marked as failure properly
    for (CustomerTask task : customerTasks) {
      assertNotNull(task.getCompletionTime());
      TaskInfo taskInfo = TaskInfo.get(task.getTaskUUID());
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    }
  }

  @Test
  public void testAutoRetryAbortedTasks() {
    List<TaskInfo> retryableTasks = new ArrayList<>();
    TaskType.filteredValues().stream()
        .filter(t -> !t.getCustomerTaskIds().isEmpty())
        .filter(t -> Commissioner.isTaskTypeRetryable(t))
        .forEach(
            t -> {
              TaskInfo taskInfo = new TaskInfo(t, null);
              taskInfo.setTaskParams(Json.newObject());
              taskInfo.setOwner("");
              taskInfo.setYbaVersion(Util.getYbaVersion());
              taskInfo.setTaskState(TaskInfo.State.Aborted);
              taskInfo.setTaskError(new YBAError(Code.PLATFORM_SHUTDOWN, "Platform shutdown"));
              taskInfo.save();
              Pair<CustomerTask.TaskType, CustomerTask.TargetType> pair =
                  Iterables.getFirst(t.getCustomerTaskIds(), null);
              CustomerTask cTask =
                  CustomerTask.create(
                      customer,
                      UUID.randomUUID(),
                      taskInfo.getUuid(),
                      pair.getSecond(),
                      pair.getFirst(),
                      "FakeTarget");
              cTask.setCompletionTime(new Date());
              cTask.save();
              retryableTasks.add(taskInfo);
            });
    Set<UUID> nonAutoRetryableTaskUuids = new HashSet<>();
    List<List<TaskInfo>> partitions =
        Lists.partition(retryableTasks, (int) Math.ceil(retryableTasks.size() / 4.0));
    // Set old version for the first partition.
    partitions.get(0).stream()
        .forEach(
            t -> {
              t.setYbaVersion("1.1.0.0-b1");
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    // Set very old task with the same version.
    partitions.get(1).stream()
        .forEach(
            t -> {
              t.setCreateTime(Date.from(Instant.now().minus(1, ChronoUnit.DAYS)));
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    // Set a different failure reason.
    partitions.get(2).stream()
        .forEach(
            t -> {
              t.setTaskError(new YBAError(Code.UNKNOWN_ERROR, "Unknown error"));
              t.save();
              nonAutoRetryableTaskUuids.add(t.getUuid());
            });
    Set<UUID> autoRetryableTaskUuids =
        partitions.get(3).stream().map(TaskInfo::getUuid).collect(Collectors.toSet());
    taskManager.autoRetryAbortedTasks(
        Duration.ofMinutes(10),
        ct -> {
          TaskInfo taskInfo = TaskInfo.getOrBadRequest(ct.getTaskUUID());
          assertFalse(
              String.format("Non retryable task %s(%s)", ct.getTaskUUID(), taskInfo),
              nonAutoRetryableTaskUuids.contains(ct.getTaskUUID()));
          assertTrue(
              String.format("Already retried task %s(%s)", ct.getTaskUUID(), taskInfo),
              autoRetryableTaskUuids.remove(ct.getTaskUUID()));
        });
    assertEquals(0, autoRetryableTaskUuids.size());
  }

  @Test
  public void testUpdateUniverseSoftwareUpgradeStateSetMarksInProgressAzFailed() {
    universe = ModelFactory.createUniverse(customer.getId());
    UUID taskUuid = UUID.randomUUID();
    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUuid);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();

    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUuid,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.SoftwareUpgradeYB,
        "universe");

    UUID azUuid = UUID.randomUUID();
    UUID clusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    AZUpgradeState inProg =
        new AZUpgradeState(
            azUuid, "az-1", ServerType.TSERVER, clusterUuid, AZUpgradeStatus.IN_PROGRESS);

    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams d = u.getUniverseDetails();
          d.placementModificationTaskUuid = taskUuid;
          d.softwareUpgradeState = UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading;
          d.prevYBSoftwareConfig = new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
          d.prevYBSoftwareConfig.getTserverAZUpgradeStatesList().add(inProg);
          u.setUniverseDetails(d);
        },
        false);

    taskManager.updateUniverseSoftwareUpgradeStateSet();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        AZUpgradeStatus.FAILED,
        universe
            .getUniverseDetails()
            .prevYBSoftwareConfig
            .getTserverAZUpgradeStatesList()
            .get(0)
            .getStatus());
  }

  private CustomerTask createYbaBackupTask(TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(taskType, null);
    taskInfo.setUuid(UUID.randomUUID());
    taskInfo.setTaskState(TaskInfo.State.Running);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.save();
    return CustomerTask.create(
        customer,
        UUID.randomUUID(),
        taskInfo.getUuid(),
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.CreateYbaBackup,
        "yba-host");
  }

  @Test
  public void testFinalizeRestoredYbaBackupTask() {
    // A concurrent one-off backup task that was also in flight when the dump was taken; its
    // outcome is unknown so it must keep the "Platform restarted" behavior.
    CustomerTask olderBackupTask = createYbaBackupTask(TaskType.CreateYbaBackup);
    DB.sqlUpdate("update task_info set create_time = :createTime where uuid = :uuid")
        .setParameter("createTime", Date.from(Instant.now().minus(1, ChronoUnit.HOURS)))
        .setParameter("uuid", olderBackupTask.getTaskUUID())
        .execute();
    // The most recently created in-flight backup task is the creator of the restored backup.
    CustomerTask creatorTask = createYbaBackupTask(TaskType.CreateContinuousBackup);
    ScheduleTask.create(creatorTask.getTaskUUID(), UUID.randomUUID());
    // An unrelated in-flight task must keep the "Platform restarted" behavior.
    CustomerTask universeTask =
        createTask(CustomerTask.TargetType.Universe, UUID.randomUUID(), Create);
    TaskInfo universeTaskInfo = TaskInfo.getOrBadRequest(universeTask.getTaskUUID());
    universeTaskInfo.setTaskState(TaskInfo.State.Running);
    universeTaskInfo.save();

    taskManager.finalizeRestoredYbaBackupTask();

    TaskInfo creatorTaskInfo = TaskInfo.getOrBadRequest(creatorTask.getTaskUUID());
    assertEquals(TaskInfo.State.Success, creatorTaskInfo.getTaskState());
    assertNull(creatorTaskInfo.getTaskError());
    assertNotNull(CustomerTask.findByTaskUUID(creatorTask.getTaskUUID()).getCompletionTime());
    // The scheduler skips runs while the last schedule task is incomplete.
    assertNotNull(ScheduleTask.fetchByTaskUUID(creatorTask.getTaskUUID()).getCompletedTime());
    // Only the creator task is finalized.
    assertEquals(
        TaskInfo.State.Running,
        TaskInfo.getOrBadRequest(olderBackupTask.getTaskUUID()).getTaskState());
    assertEquals(
        TaskInfo.State.Running,
        TaskInfo.getOrBadRequest(universeTask.getTaskUUID()).getTaskState());

    taskManager.handleAllPendingTasks();

    // The pending task sweep leaves the finalized creator task untouched and fails the rest.
    creatorTaskInfo = TaskInfo.getOrBadRequest(creatorTask.getTaskUUID());
    assertEquals(TaskInfo.State.Success, creatorTaskInfo.getTaskState());
    assertNull(creatorTaskInfo.getTaskError());
    for (CustomerTask task : Lists.newArrayList(olderBackupTask, universeTask)) {
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(task.getTaskUUID());
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
      assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
    }
  }

  @Test
  public void testIncompleteYbaBackupTaskFailsOnRestartWithoutRestore() {
    // Without a preceding restore, an incomplete backup task is a genuine crash and must
    // keep the "Platform restarted" failure.
    CustomerTask backupTask = createYbaBackupTask(TaskType.CreateContinuousBackup);
    ScheduleTask.create(backupTask.getTaskUUID(), UUID.randomUUID());

    taskManager.handleAllPendingTasks();

    TaskInfo taskInfo = TaskInfo.getOrBadRequest(backupTask.getTaskUUID());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertEquals("Platform restarted.", taskInfo.getTaskError().getMessage());
    assertNotNull(ScheduleTask.fetchByTaskUUID(backupTask.getTaskUUID()).getCompletedTime());
  }

  @Test
  public void testHandleRestoreTaskFinalizesYbaBackupTask() {
    // The in-flight backup task row that the restored DB dump always contains.
    CustomerTask backupTask = createYbaBackupTask(TaskType.CreateContinuousBackup);

    // Simulate the restore task whose rows are wiped by the DB swap: create the rows,
    // write the marker files, then delete the rows.
    TaskInfo restoreTaskInfo = new TaskInfo(TaskType.RestoreContinuousBackup, null);
    restoreTaskInfo.setUuid(UUID.randomUUID());
    restoreTaskInfo.setTaskState(TaskInfo.State.Running);
    restoreTaskInfo.setTaskParams(Json.newObject());
    restoreTaskInfo.setOwner("");
    restoreTaskInfo.setYbaVersion(Util.getYbaVersion());
    restoreTaskInfo.save();
    CustomerTask restoreCustomerTask =
        CustomerTask.create(
            customer,
            UUID.randomUUID(),
            restoreTaskInfo.getUuid(),
            CustomerTask.TargetType.Yba,
            CustomerTask.TaskType.RestoreContinuousBackup,
            "yba-host");
    Util.writeRestoreTaskInfo(restoreCustomerTask, restoreTaskInfo);
    restoreCustomerTask.delete();
    restoreTaskInfo.delete();

    taskManager.handleRestoreTask();

    // The backup task from the restored dump is finalized as succeeded.
    TaskInfo backupTaskInfo = TaskInfo.getOrBadRequest(backupTask.getTaskUUID());
    assertEquals(TaskInfo.State.Success, backupTaskInfo.getTaskState());
    assertNull(backupTaskInfo.getTaskError());
    // The restore task itself is re-inserted from the marker files.
    TaskInfo reinsertedRestoreTaskInfo = TaskInfo.getOrBadRequest(restoreTaskInfo.getUuid());
    assertEquals(TaskInfo.State.Success, reinsertedRestoreTaskInfo.getTaskState());
  }

  // Creates a failed universe task (TaskInfo in Failure state) plus its CustomerTask row.
  private CustomerTask createFailedUniverseTask(
      Universe universe,
      TaskType taskInfoType,
      CustomerTask.TaskType customerTaskType,
      JsonNode taskParams) {
    TaskInfo taskInfo = new TaskInfo(taskInfoType, null);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(taskParams);
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();
    return CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        customerTaskType,
        universe.getName());
  }

  // Puts the universe into the state a failed software upgrade leaves it in.
  private Universe markSoftwareUpgradeFailed(Universe universe, boolean rollbackAllowed) {
    PrevYBSoftwareConfig prev = new PrevYBSoftwareConfig();
    prev.setSoftwareVersion("2.21.0.0-b1");
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams d = u.getUniverseDetails();
          d.softwareUpgradeState = SoftwareUpgradeState.UpgradeFailed;
          d.isSoftwareRollbackAllowed = rollbackAllowed;
          d.prevYBSoftwareConfig = prev;
          d.updateInProgress = false;
          u.setUniverseDetails(d);
        });
  }

  private JsonNode softwareUpgradeTaskParams(Universe universe) {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.clusters = universe.getUniverseDetails().clusters;
    params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    params.ybSoftwareVersion = "2.21.0.0-b2";
    return Json.toJson(params);
  }

  @Test
  public void testRollbackFailedSoftwareUpgradeSubmitsRollbackUpgrade() {
    universe = ModelFactory.createUniverse(customer.getId());
    universe = markSoftwareUpgradeFailed(universe, true /* rollbackAllowed */);
    JsonNode taskParams = softwareUpgradeTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe,
            TaskType.SoftwareUpgradeYB,
            CustomerTask.TaskType.SoftwareUpgrade,
            taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    UUID rollbackTaskUUID = UUID.randomUUID();
    // commissioner.submit is mocked, so it does not persist the TaskInfo the real submit would.
    // Create it here to satisfy the customer_task.task_uuid -> task_info.uuid foreign key that the
    // handler's CustomerTask.create relies on.
    TaskInfo rollbackTaskInfo = new TaskInfo(TaskType.RollbackUpgrade, null);
    rollbackTaskInfo.setUuid(rollbackTaskUUID);
    rollbackTaskInfo.setTaskParams(Json.newObject());
    rollbackTaskInfo.setOwner("");
    rollbackTaskInfo.setYbaVersion(Util.getYbaVersion());
    rollbackTaskInfo.setTaskState(TaskInfo.State.Created);
    rollbackTaskInfo.save();
    when(mockCommissioner.canTaskRollback(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.RollbackUpgrade), any())).thenReturn(rollbackTaskUUID);

    CustomerTask rollbackTask =
        taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    // A failed software upgrade should be rolled back via a RollbackUpgrade (downgrade) task.
    verify(mockCommissioner).submit(eq(TaskType.RollbackUpgrade), any());
    assertEquals(CustomerTask.TaskType.RollbackUpgrade, rollbackTask.getType());
    assertEquals(universe.getUniverseUUID(), rollbackTask.getTargetUUID());
    assertEquals(rollbackTaskUUID, rollbackTask.getTaskUUID());
  }

  @Test
  public void testRollbackFailedSoftwareUpgradeRejectedWhenRollbackNotAllowed() {
    universe = ModelFactory.createUniverse(customer.getId());
    universe = markSoftwareUpgradeFailed(universe, false /* rollbackAllowed */);
    JsonNode taskParams = softwareUpgradeTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe,
            TaskType.SoftwareUpgradeYB,
            CustomerTask.TaskType.SoftwareUpgrade,
            taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollback(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);

    // Rollback eligibility here is gated by the upgrade path (isSoftwareRollbackAllowed), so an
    // ineligible universe fails fast with a clear error and no rollback task is submitted.
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("Cannot rollback software upgrade"));
    verify(mockCommissioner, times(0)).submit(eq(TaskType.RollbackUpgrade), any());
  }

  @Test
  public void testRollbackEditUniverseDisabledByRuntimeFlag() {
    // With yb.task.allow_edit_universe_rollback off (default), edit-universe rollback is rejected.
    universe = ModelFactory.createUniverse(customer.getId());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, Json.newObject());
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollback(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(Json.newObject());

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("not enabled"));
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testRollbackEditUniverseNotYetSupportedWhenEnabled() {
    // With the flag on, edit-universe rollback passes the gate but is still a placeholder
    // (PLAT-21484/21485), so the dispatch fails explicitly rather than attempt a rollback.
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.task.allow_edit_universe_rollback", "true");
    universe = ModelFactory.createUniverse(customer.getId());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, Json.newObject());
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollback(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(Json.newObject());

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("not yet supported"));
    verify(mockCommissioner, times(0)).submit(any(), any());
  }
}
