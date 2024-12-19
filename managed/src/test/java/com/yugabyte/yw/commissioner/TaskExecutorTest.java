// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.lessThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Stopwatch;
import com.google.common.collect.ImmutableSortedSet;
import com.google.common.util.concurrent.MoreExecutors;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskCache;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskExecutionListener;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import javax.inject.Inject;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class TaskExecutorTest extends PlatformGuiceApplicationBaseTest {

  private final ObjectMapper mapper = new ObjectMapper();

  private TaskExecutor taskExecutor;

  private Config mockConfig;

  private final Set<TaskType> RETRYABLE_TASKS =
      ImmutableSortedSet.of(
          TaskType.ReadOnlyKubernetesClusterCreate,
          TaskType.ReadOnlyKubernetesClusterDelete,
          TaskType.CreateKubernetesUniverse,
          TaskType.InstallYbcSoftwareOnK8s,
          TaskType.DestroyKubernetesUniverse,
          TaskType.UpdateKubernetesDiskSize,
          TaskType.CreateUniverse,
          TaskType.EditUniverse,
          TaskType.ReplaceNodeInUniverse,
          TaskType.EditKubernetesUniverse,
          TaskType.ReadOnlyClusterCreate,
          TaskType.AddNodeToUniverse,
          TaskType.RemoveNodeFromUniverse,
          TaskType.DeleteNodeFromUniverse,
          TaskType.ReleaseInstanceFromUniverse,
          TaskType.RebootNodeInUniverse,
          TaskType.MultiTableBackup,
          TaskType.BackupUniverse,
          TaskType.ResizeNode,
          TaskType.StartNodeInUniverse,
          TaskType.StopNodeInUniverse,
          TaskType.CloudProviderDelete,
          TaskType.ReinstallNodeAgent,
          TaskType.CloudBootstrap,
          TaskType.KubernetesOverridesUpgrade,
          TaskType.GFlagsKubernetesUpgrade,
          TaskType.SoftwareKubernetesUpgrade,
          TaskType.SoftwareKubernetesUpgradeYB,
          TaskType.FinalizeUpgrade,
          TaskType.RollbackUpgrade,
          TaskType.RollbackKubernetesUpgrade,
          TaskType.SoftwareUpgrade,
          TaskType.SoftwareUpgradeYB,
          TaskType.VMImageUpgrade,
          TaskType.GFlagsUpgrade,
          TaskType.RebootUniverse,
          TaskType.RestartUniverse,
          TaskType.RestartUniverseKubernetesUpgrade,
          TaskType.ThirdpartySoftwareUpgrade,
          TaskType.FinalizeUpgrade,
          TaskType.CertsRotate,
          TaskType.SystemdUpgrade,
          TaskType.ModifyAuditLoggingConfig,
          TaskType.StartMasterOnNode,
          TaskType.MasterFailover,
          TaskType.SyncMasterAddresses,
          TaskType.ReprovisionNode,
          TaskType.CloudProviderEdit,
          TaskType.SwitchoverDrConfig,
          TaskType.FailoverDrConfig,
          TaskType.ResumeKubernetesUniverse,
          TaskType.PauseKubernetesUniverse,
          TaskType.FailoverDrConfig,
          TaskType.DecommissionNode);

  @Override
  protected Application provideApplication() {
    mockConfig = mock(Config.class);
    return configureApplication(
            new GuiceApplicationBuilder()
                .disable(GuiceModule.class)
                .configure(testDatabase())
                .overrides(
                    bind(RuntimeConfigFactory.class)
                        .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
                .overrides(
                    bind(PlatformReplicationManager.class)
                        .toInstance(mock(PlatformReplicationManager.class)))
                .overrides(
                    bind(ExecutorServiceProvider.class).to(DefaultExecutorServiceProvider.class))
                .overrides(
                    bind(CustomWsClientFactory.class)
                        .toProvider(CustomWsClientFactoryProvider.class)))
        .build();
  }

  private TaskInfo waitForTask(UUID taskUUID) {
    return waitForTask(taskUUID, Duration.ofSeconds(10));
  }

  private TaskInfo waitForTask(UUID taskUUID, Duration waitTime) {
    Stopwatch watch = Stopwatch.createStarted();
    while (taskExecutor.isTaskRunning(taskUUID) && watch.elapsed().compareTo(waitTime) < 0) {
      try {
        Thread.sleep(100);
      } catch (InterruptedException e) {
      }
    }
    if (taskExecutor.isTaskRunning(taskUUID)) {
      fail("Task " + taskUUID + " did not complete in time");
    }
    return TaskInfo.getOrBadRequest(taskUUID);
  }

  private ITask mockTaskCommon(boolean abortable) {
    JsonNode node = mapper.createObjectNode();
    Class<? extends ITask> taskClass = abortable ? AbortableTask.class : NonAbortableTask.class;
    ITask task = spy(app.injector().instanceOf(taskClass));
    doReturn("TestTask").when(task).getName();
    doReturn(node).when(task).getTaskParams();
    return task;
  }

  @Abortable
  static class AbortableTask extends AbstractTaskBase {
    @Inject
    AbortableTask(BaseTaskDependencies baseTaskDependencies) {
      super(baseTaskDependencies);
    }

    @Override
    public void run() {}
  }

  @Abortable(enabled = false)
  static class NonAbortableTask extends AbstractTaskBase {
    @Inject
    NonAbortableTask(BaseTaskDependencies baseTaskDependencies) {
      super(baseTaskDependencies);
    }

    @Override
    public void run() {}
  }

  @Before
  public void setup() {
    taskExecutor = spy(app.injector().instanceOf(TaskExecutor.class));
    doAnswer(
            inv -> {
              Object[] objects = inv.getArguments();
              ITask task = (ITask) objects[0];
              // Create a new task info object.
              TaskInfo taskInfo = new TaskInfo(TaskType.BackupUniverse, null);
              taskInfo.setTaskParams(
                  RedactingService.filterSecretFields(task.getTaskParams(), RedactionTarget.APIS));
              taskInfo.setOwner("test-owner");
              taskInfo.setVersion(Util.getYbaVersion());
              return taskInfo;
            })
        .when(taskExecutor)
        .createTaskInfo(any(), any());
  }

  @Test
  public void testTaskSubmission() {
    ITask task = mockTaskCommon(false);
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    assertEquals(0, subTaskInfos.size());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testTaskFailure() {
    ITask task = mockTaskCommon(false);
    doThrow(new RuntimeException("Error occurred in task")).when(task).run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    UUID outTaskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(outTaskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    assertEquals(0, subTaskInfos.size());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String errMsg = taskInfo.getTaskError().getMessage();
    assertTrue("Found " + errMsg, errMsg.contains("Error occurred in task"));
    assertNotNull(taskInfo.getVersion());
    assertEquals(Util.getYbaVersion(), taskInfo.getVersion());
  }

  @Test
  public void testSubTaskAsyncSuccess() {
    ITask task = mockTaskCommon(false);
    ITask subTask = mockTaskCommon(false);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(1, subTasksByPosition.size());
    verify(subTask, times(1)).run();
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    assertEquals(TaskInfo.State.Success, subTaskInfos.get(0).getTaskState());
  }

  @Test
  public void testSubTaskAsyncFailure() {
    ITask task = mockTaskCommon(false);
    ITask subTask = mockTaskCommon(false);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    doThrow(new RuntimeException("Error occurred in subtask")).when(subTask).run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(1, subTasksByPosition.size());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());

    String errMsg = taskInfo.getTaskError().getMessage();
    assertTrue("Found " + errMsg, errMsg.contains("Failed to execute task"));

    assertEquals(TaskInfo.State.Failure, subTaskInfos.get(0).getTaskState());
    errMsg = subTaskInfos.get(0).getTaskError().getMessage();
    assertTrue("Found " + errMsg, errMsg.contains("Error occurred in subtask"));
  }

  @Test
  public void testSubTaskNonAbortable() {
    ITask task = mockTaskCommon(false);
    CountDownLatch latch = new CountDownLatch(1);
    doAnswer(
            inv -> {
              latch.await();
              return null;
            })
        .when(task)
        .run();

    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    try {
      assertThrows(RuntimeException.class, () -> taskExecutor.abort(taskUUID, false));
    } finally {
      latch.countDown();
    }
    waitForTask(taskUUID);
  }

  @Test
  public void testSubTaskAbort() throws InterruptedException {
    ITask task = mockTaskCommon(true);
    ITask subTask1 = mockTaskCommon(true);
    ITask subTask2 = mockTaskCommon(true);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();

    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup1 = taskExecutor.createSubTaskGroup("test1");
              subTasksGroup1.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup1);
              SubTaskGroup subTasksGroup2 = taskExecutor.createSubTaskGroup("test2");
              subTasksGroup2.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup2);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    CountDownLatch latch1 = new CountDownLatch(1);
    CountDownLatch latch2 = new CountDownLatch(1);
    doAnswer(
            inv -> {
              latch1.countDown();
              latch2.await();
              return null;
            })
        .when(subTask1)
        .run();

    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    if (!latch1.await(200, TimeUnit.SECONDS)) {
      fail();
    }
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    assertEquals(TaskInfo.State.Running, taskInfo.getTaskState());
    // Stop the task
    taskExecutor.abort(taskUUID, false);
    latch2.countDown();

    taskInfo = waitForTask(taskUUID);

    verify(subTask1, times(1)).run();
    verify(subTask2, times(0)).run();

    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(2, subTasksByPosition.size());
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    assertEquals(TaskInfo.State.Success, subTaskInfos.get(0).getTaskState());
    assertEquals(TaskInfo.State.Aborted, subTaskInfos.get(1).getTaskState());
    assertNotNull(taskInfo.getVersion());
    assertEquals(Util.getYbaVersion(), taskInfo.getVersion());
  }

  @Test
  public void testSubTaskAbortAtPosition() {
    ITask task = mockTaskCommon(true);
    ITask subTask1 = mockTaskCommon(true);
    ITask subTask2 = mockTaskCommon(true);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup1 = taskExecutor.createSubTaskGroup("test1");
              subTasksGroup1.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup1);
              SubTaskGroup subTasksGroup2 = taskExecutor.createSubTaskGroup("test2");
              subTasksGroup2.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup2);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    AtomicInteger test = new AtomicInteger(0);
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    taskRunner.setTaskExecutionListener(
        new TaskExecutionListener() {
          @Override
          public void beforeTask(TaskInfo tf) {
            test.incrementAndGet();
            if (tf.getPosition() == 1) {
              throw new CancellationException("cancelled");
            }
          }

          @Override
          public void afterTask(TaskInfo taskInfo, Throwable t) {}
        });
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    // 1 parent task + 2 subtasks.
    assertEquals(3, test.get());

    verify(subTask1, times(1)).run();
    verify(subTask2, times(0)).run();

    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    assertEquals(2, subTasksByPosition.size());
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    assertEquals(TaskInfo.State.Success, subTaskInfos.get(0).getTaskState());
    assertEquals(TaskInfo.State.Aborted, subTaskInfos.get(1).getTaskState());
  }

  @Test
  public void testRunnableTaskReset() {
    ITask task = mockTaskCommon(false);
    ITask subTask1 = mockTaskCommon(false);
    ITask subTask2 = mockTaskCommon(false);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.reset();
              subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    verify(subTask1, times(0)).run();
    verify(subTask2, times(1)).run();
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(2, subTasksByPosition.size());
    List<TaskInfo.State> subTaskStates =
        subTasksByPosition.get(0).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertEquals(TaskInfo.State.Created, subTaskStates.get(0));
    subTaskStates =
        subTasksByPosition.get(1).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertTrue(subTaskStates.contains(TaskInfo.State.Success));
  }

  @Test
  public void testShutdown() throws InterruptedException {
    ITask task = mockTaskCommon(false);
    CountDownLatch latch1 = new CountDownLatch(1);
    CountDownLatch latch2 = new CountDownLatch(1);
    AtomicBoolean executed = new AtomicBoolean();
    ExecutorService executor = Executors.newFixedThreadPool(1);
    doAnswer(
            inv -> {
              try {
                latch1.countDown();
                latch2.await();
                executed.set(true);
              } catch (InterruptedException e) {
                throw new CancellationException(e.getMessage());
              }
              return null;
            })
        .when(task)
        .run();

    // CompletableFuture.supplyAsync(() -> TaskExecutor.this.shutdown(Duration.ofMinutes(5))));
    RunnableTask taskRunner1 = taskExecutor.createRunnableTask(task, null);
    UUID taskUUID = taskExecutor.submit(taskRunner1, executor);
    // Wait for the task to be running.
    latch1.await();
    // Submit executor service shutdown to mimic shutdown hook.
    CompletableFuture.supplyAsync(
        () -> MoreExecutors.shutdownAndAwaitTermination(executor, 2, TimeUnit.SECONDS));
    // Submit task executor shutdown to mimic shutdown hook.
    CompletableFuture.supplyAsync(() -> taskExecutor.shutdown(Duration.ofSeconds(2)));
    // Wait for the task to be cancelled.
    waitForTask(taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    // Aborted due to shutdown.
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    RunnableTask taskRunner2 = taskExecutor.createRunnableTask(task, null);
    // This should get rejected as the executor is already shutdown.
    assertThrows(
        PlatformServiceException.class,
        () -> taskExecutor.submit(taskRunner2, Executors.newFixedThreadPool(1)));
    taskInfo = TaskInfo.getOrBadRequest(taskRunner2.getTaskUUID());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  @Test
  public void testRunnableTaskCallstack() {
    ITask task = mockTaskCommon(false);
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    String[] callstack = taskRunner.getCreatorCallstack();
    assertThat(
        callstack[0],
        containsString("com.yugabyte.yw.commissioner.TaskExecutor.createRunnableTask"));
    assertThat(callstack.length, lessThanOrEqualTo(16));
  }

  @Test
  public void testRunnableTaskWaitFor() throws InterruptedException {
    ITask task = mockTaskCommon(true);
    ITask subTask = mockTaskCommon(true);
    CountDownLatch latch = new CountDownLatch(1);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();

    doAnswer(
            inv -> {
              latch.countDown();
              while (true) {
                taskExecutor.getRunnableTask(taskUUIDRef.get()).waitFor(Duration.ofSeconds(1));
              }
            })
        .when(subTask)
        .run();

    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    taskUUIDRef.set(taskUUID);
    latch.await();
    taskExecutor.abort(taskUUID, false);
    TaskInfo taskInfo = waitForTask(taskUUID);
    verify(subTask).setUserTaskUUID(eq(taskUUID));
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    String errMsg = taskInfo.getSubTasks().get(0).getTaskError().getMessage();
    assertThat(errMsg, containsString("is aborted while waiting"));
  }

  @Test
  public void testRetryableAnnotation() {
    // Iterate through allowed Task Types by Commissioner only
    Set<TaskType> retryableTaskTypes =
        TaskType.filteredValues().stream()
            .filter(taskType -> TaskExecutor.isTaskRetryable(taskType.getTaskClass()))
            .collect(Collectors.toCollection(TreeSet::new));
    assertEquals(RETRYABLE_TASKS, retryableTaskTypes);
  }

  @Test
  public void testTaskCache() {
    ITask task = mockTaskCommon(false);
    ITask subTask = mockTaskCommon(false);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              TaskCache taskCache = runnable.getTaskCache();
              // Retrieve the cached data in the subtask.
              ObjectNode object = (ObjectNode) taskCache.get("key1");
              assertEquals(2, object.size());
              assertEquals("innerVal1", object.get("innerKey1").asText());
              assertEquals("innerVal2", object.get("innerKey2").asText());
              return null;
            })
        .when(subTask)
        .run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              TaskCache taskCache = runnable.getTaskCache();
              ObjectNode object = Json.newObject();
              // Put in the cache in the parent task.
              object.put("innerKey1", "innerVal1");
              object.put("innerKey2", "innerVal2");
              taskCache.put("key1", object);
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup = taskExecutor.createSubTaskGroup("test");
              subTasksGroup.addSubTask(subTask);
              runnable.addSubTaskGroup(subTasksGroup);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();

    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(1, subTasksByPosition.size());
    verify(subTask, times(1)).run();
    verify(task, times(1)).setTaskUUID(any());
    verify(subTask, times(1)).setTaskUUID(any());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    assertEquals(TaskInfo.State.Success, subTaskInfos.get(0).getTaskState());
  }

  @Test
  public void testTaskValidationFailure() {
    ITask task = mockTaskCommon(false);
    doThrow(new RuntimeException("Validation failed")).when(task).validateParams(true);
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    assertThrows(
        PlatformServiceException.class,
        () -> taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1)));
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskRunner.getTaskUUID());
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  @Test
  public void testShoudRunPredicate() {
    ITask task = mockTaskCommon(false);
    ITask subTask1 = mockTaskCommon(false);
    ITask subTask2 = mockTaskCommon(false);
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup1 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup1.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup1);
              SubTaskGroup subTasksGroup2 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup2.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup2);
              // Do not run it.
              subTasksGroup2.setShouldRunPredicate(t -> false);
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    verify(subTask1, times(1)).run();
    verify(subTask2, times(0)).run();
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(2, subTasksByPosition.size());
    List<TaskInfo.State> subTaskStates =
        subTasksByPosition.get(0).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertEquals(TaskInfo.State.Success, subTaskStates.get(0));
    subTaskStates =
        subTasksByPosition.get(1).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertTrue(subTaskStates.contains(TaskInfo.State.Success));
  }

  @Test
  public void testTaskExecutionException() {
    ITask task = mockTaskCommon(false);
    ObjectNode params = Json.newObject();
    params.put("param1", "value1");
    doAnswer(
            inv -> {
              throw new TaskExecutionException(Code.PLATFORM_RESTARTED, "Platform restarted");
            })
        .when(task)
        .run();
    doReturn(params).when(task).getTaskParams();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    waitForTask(taskUUID);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    YBAError taskError = taskInfo.getTaskError();
    assertEquals(Code.PLATFORM_RESTARTED, taskError.getCode());
    assertEquals("Platform restarted", taskError.getMessage());
    JsonNode taskParams = taskInfo.getTaskParams();
    assertEquals(params, taskParams);
  }

  @Test
  public void testHandleCompletionFailure() {
    ITask task = mockTaskCommon(false);
    ITask subTask1 = mockTaskCommon(false);
    ITask subTask2 = mockTaskCommon(false);
    doThrow(RuntimeException.class).when(subTask1).run();
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup1 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup1.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup1);
              subTasksGroup1.setAfterRunHandler(
                  (t, th) -> {
                    assertTrue(t != null);
                    assertTrue(th != null);
                    // Propagate the error to fail the task.
                    return th;
                  });
              SubTaskGroup subTasksGroup2 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup2.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup2);
              subTasksGroup2.setAfterRunHandler(
                  (t, th) -> {
                    assertTrue(t != null);
                    assertTrue(th != null);
                    // Ignore error by returning null.
                    return null;
                  });
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    verify(subTask1, times(1)).run();
    verify(subTask2, times(0)).run();
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(2, subTasksByPosition.size());
    List<TaskInfo.State> subTaskStates =
        subTasksByPosition.get(0).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertEquals(TaskInfo.State.Failure, subTaskStates.get(0));
    subTaskStates =
        subTasksByPosition.get(1).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertTrue(subTaskStates.contains(TaskInfo.State.Created));
  }

  @Test
  public void testHandleCompletionSuccess() {
    ITask task = mockTaskCommon(false);
    ITask subTask1 = mockTaskCommon(false);
    ITask subTask2 = mockTaskCommon(false);
    doThrow(RuntimeException.class).when(subTask1).run();
    doThrow(RuntimeException.class).when(subTask2).run();
    AtomicReference<UUID> taskUUIDRef = new AtomicReference<>();
    doAnswer(
            inv -> {
              RunnableTask runnable = taskExecutor.getRunnableTask(taskUUIDRef.get());
              // Invoke subTask from the parent task.
              SubTaskGroup subTasksGroup1 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup1.addSubTask(subTask1);
              runnable.addSubTaskGroup(subTasksGroup1);
              subTasksGroup1.setAfterRunHandler(
                  (t, th) -> {
                    assertTrue(t != null);
                    assertTrue(th != null);
                    // Ignore error by returning null.
                    return null;
                  });
              SubTaskGroup subTasksGroup2 = taskExecutor.createSubTaskGroup("test");
              subTasksGroup2.addSubTask(subTask2);
              runnable.addSubTaskGroup(subTasksGroup2);
              // Do not run it.
              subTasksGroup2.setAfterRunHandler(
                  (t, th) -> {
                    assertTrue(t != null);
                    assertTrue(th != null);
                    // Ignore error by returning null.
                    return null;
                  });
              runnable.runSubTasks();
              return null;
            })
        .when(task)
        .run();
    RunnableTask taskRunner = taskExecutor.createRunnableTask(task, null);
    taskUUIDRef.set(taskRunner.getTaskUUID());
    UUID taskUUID = taskExecutor.submit(taskRunner, Executors.newFixedThreadPool(1));
    TaskInfo taskInfo = waitForTask(taskUUID);
    List<TaskInfo> subTaskInfos = taskInfo.getSubTasks();
    verify(subTask1, times(1)).run();
    verify(subTask2, times(1)).run();
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTaskInfos.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(2, subTasksByPosition.size());
    List<TaskInfo.State> subTaskStates =
        subTasksByPosition.get(0).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertEquals(TaskInfo.State.Success, subTaskStates.get(0));
    subTaskStates =
        subTasksByPosition.get(1).stream().map(TaskInfo::getTaskState).collect(Collectors.toList());
    assertEquals(1, subTaskStates.size());
    assertTrue(subTaskStates.contains(TaskInfo.State.Success));
    assertNotNull(taskInfo.getVersion());
    assertEquals(Util.getYbaVersion(), taskInfo.getVersion());
  }
}
