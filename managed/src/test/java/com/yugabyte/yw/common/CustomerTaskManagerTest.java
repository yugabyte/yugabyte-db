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
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.inject.Injector;
import com.google.inject.Key;
import com.google.inject.TypeLiteral;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.rollback.TaskRollbackComputer;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.AZUpgradeStatus;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
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
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.ebean.DB;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClientApi;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerTaskManagerTest extends FakeDBApplication {
  Customer customer;
  Universe universe;
  CustomerTaskManager taskManager;
  YBClientApi mockClient;

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
    mockClient = mock(YBClientApi.class);
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
    mockClient = mock(YBClientApi.class);
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
    mockClient = mock(YBClientApi.class);
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

  // Retryability is decided in two independent places: the @ITask.Retryable annotation, which
  // drives
  // the retryable flag in the task API and therefore the Retry button, and the switch in
  // retryCustomerTask that rebuilds task params. A task type annotated but missing from the switch
  // renders a Retry button that always fails with "Invalid task type", so both halves are asserted
  // here. testAutoRetryAbortedTasks covers only the annotation half - it supplies its own retry
  // function and never reaches the switch.
  @Test
  public void testConfigureExportTelemetryConfigRetryRebuildsParams() {
    when(mockCommissioner.getTaskParams(any())).thenReturn(Json.newObject());
    for (TaskType taskType :
        List.of(
            TaskType.ConfigureExportTelemetryConfig,
            TaskType.KubernetesConfigureExportTelemetryConfig)) {
      assertTrue(
          taskType + " must be annotated @ITask.Retryable",
          Commissioner.isTaskTypeRetryable(taskType));
      assertFalse(
          taskType + " is missing a case in retryCustomerTask",
          retryFailsWithUnmappedTaskType(taskType));
    }
  }

  private boolean retryFailsWithUnmappedTaskType(TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(taskType, null);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();
    Pair<CustomerTask.TaskType, CustomerTask.TargetType> pair =
        Iterables.getFirst(taskType.getCustomerTaskIds(), null);
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
    try {
      taskManager.retryCustomerTask(customer.getUuid(), taskInfo.getUuid());
      return false;
    } catch (Exception e) {
      // Other failures are expected with these synthetic fixtures (no such universe,
      // updatingTaskUUID
      // mismatch); only an unmapped task type is the defect under test.
      return e.getMessage() != null && e.getMessage().contains("Invalid task type");
    }
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

  private JsonNode editUniverseTaskParams(Universe universe) {
    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.setUniverseUUID(universe.getUniverseUUID());
    return Json.toJson(params);
  }

  private Universe createKubernetesUniverse(String name) {
    Universe k8sUniverse =
        ModelFactory.createUniverse(name, customer.getId(), CloudType.kubernetes);
    k8sUniverse.updateConfig(Map.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    k8sUniverse.save();
    return k8sUniverse;
  }

  /** Persists a TaskInfo row so CustomerTask.create can satisfy the task_uuid FK. */
  private void persistTaskInfoPlaceholder(UUID taskUUID, TaskType taskType) {
    TaskInfo taskInfo = new TaskInfo(taskType, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.setTaskState(TaskInfo.State.Created);
    taskInfo.save();
  }

  private XClusterConfig createOldXClusterWithReplicationDone() {
    Universe source =
        ModelFactory.createUniverse("dr-source-" + UUID.randomUUID(), customer.getId());
    Universe target =
        ModelFactory.createUniverse("dr-target-" + UUID.randomUUID(), customer.getId());
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "dr-old-xcluster-" + UUID.randomUUID(),
            source.getUniverseUUID(),
            target.getUniverseUUID());
    String tableId = UUID.randomUUID().toString();
    xClusterConfig.addTables(Collections.singleton(tableId));
    xClusterConfig.updateReplicationSetupDone(Collections.singleton(tableId));
    return xClusterConfig;
  }

  private JsonNode switchoverTaskParams(XClusterConfig oldXCluster) {
    DrConfigTaskParams params = new DrConfigTaskParams();
    params.setOldXClusterConfig(oldXCluster);
    return Json.toJson(params);
  }

  @Test
  public void testRollbackSwitchoverDrConfigSubmitsSwitchoverRollback() {
    universe = ModelFactory.createUniverse(customer.getId());
    XClusterConfig oldXCluster = createOldXClusterWithReplicationDone();
    JsonNode taskParams = switchoverTaskParams(oldXCluster);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.SwitchoverDrConfig, CustomerTask.TaskType.Switchover, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    UUID rollbackTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(rollbackTaskUUID, TaskType.SwitchoverDrConfigRollback);
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.SwitchoverDrConfigRollback), any()))
        .thenReturn(rollbackTaskUUID);

    CustomerTask rollbackTask =
        taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.SwitchoverDrConfigRollback), paramsCaptor.capture());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
    assertEquals(CustomerTask.TaskType.SwitchoverRollback, rollbackTask.getType());
    assertEquals(rollbackTaskUUID, rollbackTask.getTaskUUID());
  }

  @Test
  public void testRollbackSwitchoverDrConfigRejectedWhenOldXClusterMissing() {
    universe = ModelFactory.createUniverse(customer.getId());
    JsonNode taskParams = switchoverTaskParams(null);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.SwitchoverDrConfig, CustomerTask.TaskType.Switchover, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("old xCluster config"));
    verify(mockCommissioner, times(0)).submit(any(), any());
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
    // Create it here to satisfy the customer_task.task_uuid -> task_info.uuid foreign key.
    persistTaskInfoPlaceholder(rollbackTaskUUID, TaskType.RollbackUpgrade);
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.RollbackUpgrade), any())).thenReturn(rollbackTaskUUID);

    CustomerTask rollbackTask =
        taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    // A failed software upgrade should be rolled back via a fresh RollbackUpgrade (downgrade)
    // task, not linked as a retry of the failed upgrade.
    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.RollbackUpgrade), paramsCaptor.capture());
    assertNull(paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
    assertEquals(CustomerTask.TaskType.RollbackUpgrade, rollbackTask.getType());
    assertEquals(universe.getUniverseUUID(), rollbackTask.getTargetUUID());
    assertEquals(rollbackTaskUUID, rollbackTask.getTaskUUID());
  }

  @Test
  public void testRollbackFailedSoftwareKubernetesUpgradeSubmitsRollbackKubernetesUpgrade() {
    universe = createKubernetesUniverse("k8s-upgrade-" + UUID.randomUUID());
    universe = markSoftwareUpgradeFailed(universe, true /* rollbackAllowed */);
    JsonNode taskParams = softwareUpgradeTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe,
            TaskType.SoftwareKubernetesUpgradeYB,
            CustomerTask.TaskType.SoftwareUpgrade,
            taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    UUID rollbackTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(rollbackTaskUUID, TaskType.RollbackKubernetesUpgrade);
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.RollbackKubernetesUpgrade), any()))
        .thenReturn(rollbackTaskUUID);

    CustomerTask rollbackTask =
        taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.RollbackKubernetesUpgrade), paramsCaptor.capture());
    assertNull(paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
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
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
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
    JsonNode taskParams = editUniverseTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("not enabled"));
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testRetryEditUniverseSetsPreviousAndOriginalTaskUUID() {
    universe = ModelFactory.createUniverse(customer.getId());
    JsonNode taskParams = editUniverseTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    // Retryability requires the failed task to own placement modification.
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().placementModificationTaskUuid = failedTaskUUID;
          u.getUniverseDetails().updateInProgress = false;
        });
    UUID retryTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(retryTaskUUID, TaskType.EditUniverse);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.EditUniverse), any())).thenReturn(retryTaskUUID);

    CustomerTask retryTask = taskManager.retryCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.EditUniverse), paramsCaptor.capture());
    // First retry: failed task is the chain root.
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
    assertEquals(CustomerTask.TaskType.Update, retryTask.getType());
    assertEquals(retryTaskUUID, retryTask.getTaskUUID());
  }

  @Test
  public void testRetryEditUniverseCarriesRootOriginalTaskUUID() {
    universe = ModelFactory.createUniverse(customer.getId());
    UUID rootTaskUUID = UUID.randomUUID();
    ObjectNode taskParams = (ObjectNode) editUniverseTaskParams(universe);
    // Simulate a prior retry: root is A, immediate predecessor for inherit is also A on this B.
    taskParams.put("originalTaskUUID", rootTaskUUID.toString());
    taskParams.put("previousTaskUUID", rootTaskUUID.toString());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().placementModificationTaskUuid = failedTaskUUID;
          u.getUniverseDetails().updateInProgress = false;
        });
    UUID retryTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(retryTaskUUID, TaskType.EditUniverse);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.EditUniverse), any())).thenReturn(retryTaskUUID);

    taskManager.retryCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.EditUniverse), paramsCaptor.capture());
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(rootTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
  }

  @Test
  public void testRollbackEditUniverseSubmitsRollbackEditUniverseWhenEnabled() {
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.task.allow_edit_universe_rollback", "true");
    universe = ModelFactory.createUniverse(customer.getId());
    JsonNode taskParams = editUniverseTaskParams(universe);
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    UUID rollbackTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(rollbackTaskUUID, TaskType.RollbackEditUniverse);
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.RollbackEditUniverse), any()))
        .thenReturn(rollbackTaskUUID);

    CustomerTask rollbackTask =
        taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.RollbackEditUniverse), paramsCaptor.capture());
    assertNull(paramsCaptor.getValue().getPreviousTaskUUID());
    // First failure in chain: failed task is the root.
    assertEquals(failedTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
    assertEquals(CustomerTask.TaskType.RollbackEditUniverse, rollbackTask.getType());
    assertEquals(rollbackTaskUUID, rollbackTask.getTaskUUID());
  }

  @Test
  public void testRollbackEditUniverseCarriesRootOriginalTaskUUID() {
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.task.allow_edit_universe_rollback", "true");
    universe = ModelFactory.createUniverse(customer.getId());
    UUID rootTaskUUID = UUID.randomUUID();
    ObjectNode taskParams = (ObjectNode) editUniverseTaskParams(universe);
    // Failed retry C already carries root A from the chain.
    taskParams.put("originalTaskUUID", rootTaskUUID.toString());
    taskParams.put("previousTaskUUID", UUID.randomUUID().toString());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.EditUniverse, CustomerTask.TaskType.Update, taskParams);
    UUID failedTaskUUID = failedTask.getTaskUUID();
    UUID rollbackTaskUUID = UUID.randomUUID();
    persistTaskInfoPlaceholder(rollbackTaskUUID, TaskType.RollbackEditUniverse);
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(taskParams);
    when(mockCommissioner.submit(eq(TaskType.RollbackEditUniverse), any()))
        .thenReturn(rollbackTaskUUID);

    taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID);

    ArgumentCaptor<ITaskParams> paramsCaptor = ArgumentCaptor.forClass(ITaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.RollbackEditUniverse), paramsCaptor.capture());
    assertNull(paramsCaptor.getValue().getPreviousTaskUUID());
    assertEquals(rootTaskUUID, paramsCaptor.getValue().getOriginalTaskUUID());
  }

  @Test
  public void testRollbackEditKubernetesUniverseNotYetSupported() {
    // K8s edit is not bound in TaskRollbackModule until RollbackEditKubernetesUniverse lands.
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.task.allow_edit_universe_rollback", "true");
    universe = createKubernetesUniverse("k8s-edit-" + UUID.randomUUID());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe,
            TaskType.EditKubernetesUniverse,
            CustomerTask.TaskType.Update,
            Json.newObject());
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(Json.newObject());

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("not implemented"));
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testTaskRollbackComputerRegistryBindings() {
    Injector guiceInjector = app.injector().instanceOf(Injector.class);
    Map<TaskType, TaskRollbackComputer> computers =
        guiceInjector.getInstance(
            Key.get(new TypeLiteral<Map<TaskType, TaskRollbackComputer>>() {}));
    assertNotNull(computers.get(TaskType.SwitchoverDrConfig));
    assertNotNull(computers.get(TaskType.SoftwareUpgradeYB));
    assertNotNull(computers.get(TaskType.SoftwareKubernetesUpgradeYB));
    assertNotNull(computers.get(TaskType.EditUniverse));
    assertNull(computers.get(TaskType.EditKubernetesUniverse));
    assertNull(computers.get(TaskType.CreateUniverse));
  }

  @Test
  public void testRollbackUnregisteredTaskTypeNotImplemented() {
    // Force eligibility for a TaskType with no TaskRollbackComputer binding.
    universe = ModelFactory.createUniverse(customer.getId());
    CustomerTask failedTask =
        createFailedUniverseTask(
            universe, TaskType.CreateUniverse, CustomerTask.TaskType.Create, Json.newObject());
    UUID failedTaskUUID = failedTask.getTaskUUID();
    when(mockCommissioner.canTaskRollbackDetailed(any())).thenReturn(true);
    when(mockCommissioner.getTaskParams(failedTaskUUID)).thenReturn(Json.newObject());

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> taskManager.rollbackCustomerTask(customer.getUuid(), failedTaskUUID));
    assertTrue(ex.getMessage().contains("not implemented"));
    verify(mockCommissioner, times(0)).submit(any(), any());
  }
}
