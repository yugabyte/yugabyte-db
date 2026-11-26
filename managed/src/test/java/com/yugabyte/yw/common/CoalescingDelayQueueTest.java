// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;

public class CoalescingDelayQueueTest {
  private static final Duration LONG_DELAY = Duration.ofHours(1);
  private static final Duration SHORT_DELAY = Duration.ofMillis(200);
  // Window to confirm that nothing else runs. Must be longer than SHORT_DELAY.
  private static final long QUIET_PERIOD_MS = 3 * SHORT_DELAY.toMillis();
  private static final long WAIT_TIMEOUT_SECS = 10;

  @Test
  public void testOfferCoalesces() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    assertTrue(queue.offer("a"));
    // Dropped but still reported as accepted.
    assertTrue(queue.offer("b"));
    assertTrue(queue.add("c"));
    assertEquals(1, queue.size());
    assertEquals(0, queue.remainingCapacity());
    assertEquals("a", queue.peek());
  }

  @Test
  public void testOfferNull() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    assertThrows(NullPointerException.class, () -> queue.offer(null));
  }

  @Test
  public void testPollBeforeDelay() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    assertNull(queue.poll());
    queue.offer("a");
    assertNull(queue.poll());
    // peek ignores the delay.
    assertEquals("a", queue.peek());
    assertEquals(1, queue.size());
  }

  @Test
  public void testPollAfterDelay() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(Duration.ZERO);
    queue.offer("a");
    assertEquals("a", queue.poll());
    assertTrue(queue.isEmpty());
    assertEquals(1, queue.remainingCapacity());
  }

  @Test
  public void testTakeWaitsForDelay() throws InterruptedException {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(SHORT_DELAY);
    long startNanos = System.nanoTime();
    queue.offer("a");
    assertEquals("a", queue.take());
    long elapsedMs = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
    assertTrue("Elapsed " + elapsedMs + "ms", elapsedMs >= SHORT_DELAY.toMillis());
    assertTrue(queue.isEmpty());
  }

  @Test
  public void testTimedPollTimesOutBeforeDelay() throws InterruptedException {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    queue.offer("a");
    assertNull(queue.poll(50, TimeUnit.MILLISECONDS));
    // The element is still pending.
    assertEquals("a", queue.peek());
  }

  @Test
  public void testTimedPollOnEmptyQueue() throws InterruptedException {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(Duration.ZERO);
    assertNull(queue.poll(50, TimeUnit.MILLISECONDS));
  }

  @Test
  public void testTimedPollWakesOnOffer() throws Exception {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(Duration.ZERO);
    ExecutorService consumer = Executors.newSingleThreadExecutor();
    try {
      Future<String> result =
          consumer.submit(() -> queue.poll(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
      queue.offer("a");
      assertEquals("a", result.get(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
    } finally {
      consumer.shutdownNow();
    }
  }

  @Test
  public void testTakeIsInterruptible() throws Exception {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    queue.offer("a");
    CountDownLatch interrupted = new CountDownLatch(1);
    Thread consumer =
        new Thread(
            () -> {
              try {
                queue.take();
              } catch (InterruptedException e) {
                interrupted.countDown();
              }
            });
    consumer.start();
    consumer.interrupt();
    assertTrue(interrupted.await(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
    assertEquals("a", queue.peek());
  }

  @Test
  public void testRemoveAndClear() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    assertFalse(queue.remove("a"));
    queue.offer("a");
    assertFalse(queue.remove("b"));
    assertFalse(queue.remove(null));
    assertTrue(queue.remove("a"));
    assertTrue(queue.isEmpty());
    // A new element can be offered once the slot is free.
    queue.offer("b");
    assertEquals("b", queue.peek());
    queue.clear();
    assertTrue(queue.isEmpty());
  }

  @Test
  public void testDrainToIgnoresDelay() {
    CoalescingDelayQueue<String> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    List<String> drained = new ArrayList<>();
    assertEquals(0, queue.drainTo(drained));
    queue.offer("a");
    assertEquals(0, queue.drainTo(drained, 0));
    assertEquals(1, queue.drainTo(drained));
    assertEquals(Collections.singletonList("a"), drained);
    assertTrue(queue.isEmpty());
    assertThrows(NullPointerException.class, () -> queue.drainTo(null));
    assertThrows(IllegalArgumentException.class, () -> queue.drainTo(queue));
  }

  @Test
  public void testExecutorRunsCoalescedTasksOnceAfterDelay() throws InterruptedException {
    // Same shape as PlatformExecutorFactory.createCoalescingDelayExecutor.
    ThreadPoolExecutor executor = newExecutor();
    try {
      AtomicInteger runs = new AtomicInteger();
      CountDownLatch firstRun = new CountDownLatch(1);
      Runnable task =
          () -> {
            runs.incrementAndGet();
            firstRun.countDown();
          };
      long startNanos = System.nanoTime();
      for (int i = 0; i < 5; i++) {
        executor.execute(task);
      }
      assertTrue(firstRun.await(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
      long elapsedMs = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
      assertTrue("Elapsed " + elapsedMs + "ms", elapsedMs >= SHORT_DELAY.toMillis());
      Thread.sleep(QUIET_PERIOD_MS);
      assertEquals(1, runs.get());
    } finally {
      executor.shutdownNow();
    }
  }

  @Test
  public void testExecutorRunsOnceMoreForTasksSubmittedDuringRun() throws InterruptedException {
    ThreadPoolExecutor executor = newExecutor();
    try {
      AtomicInteger runs = new AtomicInteger();
      CountDownLatch firstRunStarted = new CountDownLatch(1);
      CountDownLatch releaseFirstRun = new CountDownLatch(1);
      CountDownLatch secondRun = new CountDownLatch(1);
      Runnable task =
          () -> {
            if (runs.incrementAndGet() == 1) {
              firstRunStarted.countDown();
              try {
                releaseFirstRun.await();
              } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
              }
            } else {
              secondRun.countDown();
            }
          };
      executor.execute(task);
      assertTrue(firstRunStarted.await(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
      // Submissions while the task is running are merged into one more run.
      for (int i = 0; i < 3; i++) {
        executor.execute(task);
      }
      releaseFirstRun.countDown();
      assertTrue(secondRun.await(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
      Thread.sleep(QUIET_PERIOD_MS);
      assertEquals(2, runs.get());
    } finally {
      executor.shutdownNow();
    }
  }

  @Test
  public void testExecutorShutdownNowReturnsPendingTask() throws InterruptedException {
    CoalescingDelayQueue<Runnable> queue = new CoalescingDelayQueue<>(LONG_DELAY);
    ThreadPoolExecutor executor =
        new ThreadPoolExecutor(0, 1, LONG_DELAY.toMinutes() + 1, TimeUnit.MINUTES, queue);
    Runnable task = () -> {};
    executor.execute(task);
    List<Runnable> pending = executor.shutdownNow();
    assertEquals(1, pending.size());
    assertSame(task, pending.get(0));
    assertTrue(executor.awaitTermination(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
  }

  @Test
  public void testPlatformExecutorDelaysFirstTask() throws InterruptedException {
    // PlatformThreadPoolExecutor must not hand the first task to a new thread directly
    // (e.g the queue wrapper does when the pool can grow), which would skip the delay.
    ThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            0,
            1,
            SHORT_DELAY.plusMinutes(1).getSeconds(),
            TimeUnit.SECONDS,
            new CoalescingDelayQueue<>(SHORT_DELAY));
    try {
      AtomicInteger runs = new AtomicInteger();
      CountDownLatch firstRun = new CountDownLatch(1);
      long startNanos = System.nanoTime();
      for (int i = 0; i < 5; i++) {
        executor.execute(
            () -> {
              runs.incrementAndGet();
              firstRun.countDown();
            });
      }
      assertTrue(firstRun.await(WAIT_TIMEOUT_SECS, TimeUnit.SECONDS));
      long elapsedMs = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - startNanos);
      // This makes sure the execution was delayed.
      assertTrue("Elapsed " + elapsedMs + "ms", elapsedMs >= SHORT_DELAY.toMillis());
      Thread.sleep(QUIET_PERIOD_MS);
      assertEquals(1, runs.get());
    } finally {
      executor.shutdownNow();
    }
  }

  private static ThreadPoolExecutor newExecutor() {
    return new ThreadPoolExecutor(
        0,
        1,
        SHORT_DELAY.plusMinutes(1).toMillis(),
        TimeUnit.MILLISECONDS,
        new CoalescingDelayQueue<>(SHORT_DELAY));
  }
}
