// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.hamcrest.Matchers.greaterThan;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import javax.persistence.PersistenceException;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.modules.swagger.SwaggerModule;
import play.test.Helpers;

public class TransactionUtilTest extends PlatformGuiceApplicationBaseTest {
  private final ObjectMapper mapper = new ObjectMapper();

  @Override
  protected Application provideApplication() {
    return super.configureApplication(
            new GuiceApplicationBuilder()
                .disable(SwaggerModule.class)
                .disable(GuiceModule.class)
                .configure((Map) Helpers.inMemoryDatabase()))
        .build();
  }

  @Test
  public void testTransactionRollback() {
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateUniverse);
    taskInfo.setTaskDetails(mapper.createObjectNode());
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
    taskInfo.setTaskDetails(mapper.createObjectNode());
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
    AtomicInteger attemptCount = new AtomicInteger();
    int count =
        TransactionUtil.doInTxn(
            () -> {
              int c = attemptCount.incrementAndGet();
              if (c <= 2) {
                throw new PersistenceException(
                    "could not serialize access due to concurrent update");
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
      taskInfo.setTaskDetails(mapper.createObjectNode());
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
                          Thread.sleep(100);
                        } catch (InterruptedException e) {
                        }
                        tf.setTaskState(State.Running);
                        tf.markAsDirty();
                        tf.save();
                      }
                    },
                    null);
              });
      futures.add(future);
    }
    futures
        .stream()
        .forEach(
            f -> {
              try {
                f.get();
              } catch (Exception e) {
                fail();
              }
            });
  }
}
