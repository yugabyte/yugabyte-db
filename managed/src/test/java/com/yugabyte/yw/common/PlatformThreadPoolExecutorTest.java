// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class PlatformThreadPoolExecutorTest {

  // Basic functionality test to check if the queuing is
  // done correctly
  @Test
  public void testBasic() throws InterruptedException {
    BlockingQueue<Runnable> queue = new LinkedBlockingQueue<>(1);
    CountDownLatch latch = new CountDownLatch(1);
    CountDownLatch execCount = new CountDownLatch(3);
    PlatformThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            1, // core pool size
            2, // max pool size
            10,
            TimeUnit.SECONDS,
            queue);
    final CountDownLatch latch1 = new CountDownLatch(1);
    // Submit task 1
    executor.submit(
        () -> {
          try {
            latch1.countDown();
            latch.await();
            execCount.countDown();
          } catch (InterruptedException e) {
            throw new RuntimeException(e);
          }
        });
    // Wait for task to be picked up by a thread
    latch1.await();
    assertEquals(1, executor.getActiveCount());
    final CountDownLatch latch2 = new CountDownLatch(1);
    // Submit task 2
    executor.submit(
        () -> {
          try {
            latch2.countDown();
            latch.await();
            execCount.countDown();
          } catch (InterruptedException e) {
            throw new RuntimeException(e);
          }
        });
    // Wait for task to be picked up by another thread
    latch2.await();
    assertEquals(2, executor.getActiveCount());
    // All the two max threads are consumed
    // Submit task 3, it must be accepted as it is queued up
    executor.submit(
        () -> {
          try {
            latch.await();
            execCount.countDown();
          } catch (InterruptedException e) {
            throw new RuntimeException(e);
          }
        });

    // Verify that it is queued up
    assertEquals(1, queue.size());
    // Submit task 4, it must be rejected because queue is full
    assertThrows(
        RejectedExecutionException.class,
        () -> {
          executor.submit(
              () -> {
                // No op
              });
        });
    latch.countDown();
    // All including the queued one must get executed
    // Wait for all of them to complete otherwise exception is thrown
    execCount.await(10, TimeUnit.SECONDS);
    executor.shutdown();
  }

  // The number of tasks is more than the core pool size but
  // lesser than the max pool size.
  // Only the required number of threads must be created.
  @Test
  public void testTaskCountUnderMaxPool() throws InterruptedException {
    int taskCount = 20;
    BlockingQueue<Runnable> queue = new LinkedBlockingQueue<>(100);
    CountDownLatch startedCount = new CountDownLatch(taskCount);
    CountDownLatch latch = new CountDownLatch(1);
    CountDownLatch execCount = new CountDownLatch(taskCount);
    PlatformThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            5, // core pool size
            50, // max pool size
            10,
            TimeUnit.SECONDS,
            queue);
    for (int i = 0; i < taskCount; i++) {
      executor.submit(
          () -> {
            try {
              startedCount.countDown();
              latch.await();
              execCount.countDown();
            } catch (InterruptedException e) {
              throw new RuntimeException(e);
            }
          });
    }
    startedCount.await(10, TimeUnit.SECONDS);
    latch.countDown();
    // Wait for all of them to complete otherwise exception is thrown
    execCount.await(10, TimeUnit.SECONDS);
    // Only the required pool size must be created
    assertEquals(20, executor.getLargestPoolSize());
    executor.shutdown();
  }

  // The number of tasks is more than the max pool size.
  // Number of threads must be equal to the max pool size
  // and the rest must be queued
  @Test
  public void testTaskCountOverMaxPool() throws InterruptedException {
    int taskCount = 100;
    BlockingQueue<Runnable> queue = new LinkedBlockingQueue<>(100);
    CountDownLatch latch = new CountDownLatch(1);
    CountDownLatch execCount = new CountDownLatch(taskCount);
    PlatformThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            5, // core pool size
            60, // max pool size
            10,
            TimeUnit.SECONDS,
            queue);
    for (int i = 0; i < taskCount; i++) {
      executor.submit(
          () -> {
            try {
              latch.await();
              execCount.countDown();
            } catch (InterruptedException e) {
              throw new RuntimeException(e);
            }
          });
    }
    latch.countDown();
    // Wait for all of them to complete otherwise exception is thrown
    execCount.await(10, TimeUnit.SECONDS);
    // Max pool size must be hit
    assertEquals(60, executor.getLargestPoolSize());
    executor.shutdown();
  }

  // Submits a large number of tasks and wait for completion
  // of all of them
  @Test
  public void testStress() throws InterruptedException, ExecutionException {
    int taskCount = 5000;
    BlockingQueue<Runnable> queue = new LinkedBlockingQueue<>(10000);
    CountDownLatch execCount = new CountDownLatch(taskCount);
    ExecutorService taskSubmitterService = Executors.newFixedThreadPool(500);
    PlatformThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            10, // core pool size
            200, // max pool size
            10,
            TimeUnit.SECONDS,
            queue);
    List<Future<?>> futures = new ArrayList<>(taskCount);
    for (int i = 0; i < taskCount; i++) {
      Future<?> future =
          taskSubmitterService.submit(
              () -> {
                executor.submit(
                    () -> {
                      try {
                        // Consume some time
                        Thread.sleep(300);
                      } catch (InterruptedException e) {
                      }
                      execCount.countDown();
                    });
              });
      futures.add(future);
    }
    // Wait for all submissions
    for (Future<?> future : futures) {
      future.get();
    }
    taskSubmitterService.shutdown();
    // Wait for all of them to complete otherwise exception is thrown
    execCount.await(10, TimeUnit.SECONDS);
    // All are done but make sure the max pool size is hit
    assertEquals(200, executor.getLargestPoolSize());
    executor.shutdown();
  }
}
