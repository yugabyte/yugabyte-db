// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskParams;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

@RunWith(MockitoJUnitRunner.class)
public class TaskQueueTest extends PlatformGuiceApplicationBaseTest {
  private final int queueCapacity = 10;

  private TaskQueue taskQueue;

  private ITaskParams mockITaskParams;

  private Config mockConfig;

  @Override
  protected Application provideApplication() {
    mockConfig = mock(Config.class);
    return configureApplication(
            new GuiceApplicationBuilder()
                .disable(GuiceModule.class)
                .configure(testDatabase())
                .overrides(
                    bind(CustomWsClientFactory.class)
                        .toProvider(CustomWsClientFactoryProvider.class)))
        .build();
  }

  @Before
  public void setup() {
    taskQueue = new TaskQueue(queueCapacity);
    mockITaskParams = mock(ITaskParams.class);
  }

  private Function<TaskParams, RunnableTask> createRunnableTaskCreator(
      Runnable runnable, boolean queueable) {
    return p -> {
      RunnableTask taskRunnable = mock(RunnableTask.class);
      ITask task = mock(ITask.class);
      when(task.getQueueWaitTime(any(), any()))
          .thenReturn(queueable ? Duration.ofMillis(100) : null);
      when(taskRunnable.getTaskUUID()).thenReturn(p.getTaskUuid());
      when(taskRunnable.getTaskType()).thenReturn(p.getTaskType());
      when(taskRunnable.getTask()).thenReturn(task);
      doAnswer(
              inv -> {
                runnable.run();
                return null;
              })
          .when(taskRunnable)
          .run();
      return taskRunnable;
    };
  }

  private BiConsumer<RunnableTask, ITaskParams> createRunnableTaskRunner(
      Consumer<RunnableTask> onCompletion) {
    final AtomicReference<BiConsumer<RunnableTask, ITaskParams>> taskRunnerRef =
        new AtomicReference<>();
    BiConsumer<RunnableTask, ITaskParams> taskRunner =
        (t, p) -> {
          when(t.isRunning()).thenReturn(true);
          new Thread(
                  () -> {
                    try {
                      // Run the actual task.
                      t.run();
                    } catch (Exception e) {
                      fail("Exception occurred - " + e.getMessage());
                    } finally {
                      when(t.hasTaskCompleted()).thenReturn(true);
                      // Dequeue and submit the next.
                      taskQueue.dequeue(t, taskRunnerRef.get());
                      onCompletion.accept(t);
                    }
                  })
              .start();
        };
    taskRunnerRef.set(taskRunner);
    return taskRunner;
  }

  private ITaskParams createTaskParams(UUID targetUuid) {
    ITaskParams mockITaskParams = mock(ITaskParams.class);
    when(mockITaskParams.getTargetUuid()).thenReturn(targetUuid);
    return mockITaskParams;
  }

  @Test
  public void testTaskQueuing() throws Exception {
    int taskCount = 10;
    UUID targetUuid = UUID.randomUUID();
    CountDownLatch allCompleted = new CountDownLatch(taskCount);
    AtomicInteger parallelTaskCounter = new AtomicInteger();
    Function<TaskParams, RunnableTask> runnableTaskCreator =
        createRunnableTaskCreator(
            () -> {
              try {
                assertTrue(
                    "Only one task at a time must be running for a target",
                    parallelTaskCounter.incrementAndGet() == 1);
                // Spend some time.
                Thread.sleep(200);
              } catch (Exception e) {
                fail("Exception occurred - " + e.getMessage());
              }
            },
            true);
    BiConsumer<RunnableTask, ITaskParams> runnableTaskRunner =
        createRunnableTaskRunner(
            t -> {
              parallelTaskCounter.decrementAndGet();
              allCompleted.countDown();
            });
    for (int i = 0; i < taskCount; i++) {
      taskQueue.enqueue(
          TaskParams.builder()
              .taskParams(createTaskParams(targetUuid))
              .taskType(TaskType.CreateUniverse)
              .taskUuid(UUID.randomUUID())
              .build(),
          runnableTaskCreator,
          runnableTaskRunner);
    }
    // This ensures all are executed.
    allCompleted.await(10, TimeUnit.SECONDS);
    assertEquals(0, taskQueue.size(targetUuid));
  }

  @Test
  public void testTaskQueueOverflow() throws Exception {
    UUID targetUuid = UUID.randomUUID();
    Function<TaskParams, RunnableTask> runnableTaskCreator =
        createRunnableTaskCreator(() -> {}, true);
    // Queue up tasks without running and dequeuing.
    for (int i = 0; i < queueCapacity; i++) {
      taskQueue.enqueue(
          TaskParams.builder()
              .taskParams(createTaskParams(targetUuid))
              .taskType(TaskType.CreateUniverse)
              .taskUuid(UUID.randomUUID())
              .build(),
          runnableTaskCreator,
          (t, p) -> {
            when(t.isRunning()).thenReturn(true);
          });
    }
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                taskQueue.enqueue(
                    TaskParams.builder()
                        .taskParams(createTaskParams(targetUuid))
                        .taskType(TaskType.CreateUniverse)
                        .taskUuid(UUID.randomUUID())
                        .build(),
                    runnableTaskCreator,
                    (t, p) -> {}));
    assertEquals("Queue is already full with max capacity 10", exception.getMessage());
  }

  @Test
  public void testTaskNonQueueable() throws Exception {
    UUID targetUuid = UUID.randomUUID();
    taskQueue.enqueue(
        TaskParams.builder()
            .taskParams(createTaskParams(targetUuid))
            .taskType(TaskType.CreateUniverse)
            .taskUuid(UUID.randomUUID())
            .build(),
        createRunnableTaskCreator(() -> {}, false),
        (t, p) -> {});
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                taskQueue.enqueue(
                    TaskParams.builder()
                        .taskParams(createTaskParams(targetUuid))
                        .taskType(TaskType.CreateUniverse)
                        .taskUuid(UUID.randomUUID())
                        .build(),
                    createRunnableTaskCreator(() -> {}, false),
                    (t, p) -> {}));
    assertEquals(
        "Task CreateUniverse cannot be queued on existing task CreateUniverse",
        exception.getMessage());
    assertEquals(1, taskQueue.size(targetUuid));
  }

  @Test
  public void testTaskAbort() throws Exception {
    UUID targetUuid = UUID.randomUUID();
    Function<TaskParams, RunnableTask> runnableTaskCreator =
        createRunnableTaskCreator(() -> {}, true);
    AtomicReference<RunnableTask> runnableTaskRef = new AtomicReference<>();
    taskQueue.enqueue(
        TaskParams.builder()
            .taskParams(createTaskParams(targetUuid))
            .taskType(TaskType.CreateUniverse)
            .taskUuid(UUID.randomUUID())
            .build(),
        runnableTaskCreator,
        (t, p) -> {
          when(t.isRunning()).thenReturn(true);
          runnableTaskRef.set(t);
        });
    taskQueue.enqueue(
        TaskParams.builder()
            .taskParams(createTaskParams(targetUuid))
            .taskType(TaskType.CreateUniverse)
            .taskUuid(UUID.randomUUID())
            .build(),
        runnableTaskCreator,
        (t, p) -> {});
    assertNotNull("First task must be run", runnableTaskRef.get());
    verify(runnableTaskRef.get(), times(1)).abort(any());
  }
}
