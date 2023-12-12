// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import jakarta.persistence.PersistenceException;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import kamon.instrumentation.play.GuiceModule;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

@Slf4j
public class TransactionUtilTest extends PlatformGuiceApplicationBaseTest {
  private final ObjectMapper mapper = new ObjectMapper();
  private Config mockConfig;

  @Override
  protected Application provideApplication() {
    mockConfig = mock(Config.class);
    when(mockConfig.getString(anyString())).thenReturn("");
    return super.configureApplication(
            new GuiceApplicationBuilder().disable(GuiceModule.class).configure(testDatabase()))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  @Test
  public void testTransactionRollback() {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
    taskInfo.setDetails(mapper.createObjectNode());
    taskInfo.setOwner("test");
    taskInfo.setTaskState(State.Created);
    taskInfo.save();
    UUID taskUUID = taskInfo.getTaskUUID();
    assertThrows(
        RuntimeException.class,
        () -> {
          TransactionUtil.doInTxn(
              () -> {
                TaskInfo tf = TaskInfo.get(taskUUID);
                tf.setTaskState(State.Running);
                tf.save();
                throw new RuntimeException("Fail!");
              },
              null);
        });
    taskInfo = TaskInfo.get(taskUUID);
    assertEquals(State.Created, taskInfo.getTaskState());
  }

  @Test
  public void testTransactionCommit() {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
    taskInfo.setDetails(mapper.createObjectNode());
    taskInfo.setOwner("test");
    taskInfo.setTaskState(State.Created);
    taskInfo.save();
    UUID taskUUID = taskInfo.getTaskUUID();
    TransactionUtil.doInTxn(
        () -> {
          TaskInfo tf = TaskInfo.get(taskUUID);
          tf.setTaskState(State.Running);
          tf.save();
        },
        null);
    taskInfo = TaskInfo.get(taskUUID);
    assertEquals(State.Running, taskInfo.getTaskState());
  }

  @Test
  public void testTransactionRetry() {
    testTransactionRetry("could not serialize access due to concurrent update");
  }

  @Test
  public void testTransactionRetryH2() {
    testTransactionRetry("Deadlock detected. The current transaction was rolled back.");
  }

  private void testTransactionRetry(String exceptionMessage) {
    AtomicInteger attemptCount = new AtomicInteger();
    int count =
        TransactionUtil.doInTxn(
            () -> {
              int c = attemptCount.incrementAndGet();
              if (c <= 2) {
                throw new PersistenceException(exceptionMessage);
              }
            },
            TransactionUtil.DEFAULT_RETRY_CONFIG);
    // First two attempts run into serialization exception.
    assertEquals(3, count);
  }

  // This needs to be performed on postgres.
  @Test
  public void testTransaction() {
    List<TaskInfo> tasks = new ArrayList<>();
    for (int i = 0; i < 5; i++) {
      TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
      taskInfo.setDetails(mapper.createObjectNode());
      taskInfo.setOwner("test" + i);
      taskInfo.setTaskState(State.Created);
      taskInfo.save();
      tasks.add(taskInfo);
    }
    ExecutorService executor = Executors.newFixedThreadPool(5);
    List<Future<?>> futures = new ArrayList<>();
    for (int i = 0; i < 20; i++) {
      Future<?> future =
          executor.submit(
              () -> {
                TransactionUtil.doInTxn(
                    () -> {
                      for (TaskInfo taskInfo : tasks) {
                        TaskInfo tf = TaskInfo.get(taskInfo.getTaskUUID());
                        try {
                          Thread.sleep(10);
                        } catch (InterruptedException e) {
                        }
                        tf.setTaskState(State.Running);
                        tf.markAsDirty();
                        tf.save();
                      }
                    },
                    TransactionUtil.DEFAULT_RETRY_CONFIG);
              });
      futures.add(future);
    }
    futures.forEach(
        f -> {
          try {
            f.get();
          } catch (Exception e) {
            log.error("Failed to get future", e);
            fail();
          }
        });
  }
}
