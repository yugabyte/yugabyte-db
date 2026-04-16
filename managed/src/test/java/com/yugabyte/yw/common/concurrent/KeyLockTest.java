// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.concurrent;

import static org.junit.Assert.assertEquals;

import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;
import org.junit.Before;
import org.junit.Test;

public class KeyLockTest {

  private UUID key;
  private KeyLock<UUID> keyLock;

  @Before
  public void init() {
    key = UUID.randomUUID();
    keyLock = new KeyLock<>();
  }

  @Test
  public void testLock() throws InterruptedException {
    CountDownLatch latch = new CountDownLatch(40);
    int[] counter = new int[1];
    startThreads(latch, getLocker(false), 20, 100, counter, true);
    startThreads(latch, getLocker(false), 20, 100, counter, false);
    latch.await(30, TimeUnit.SECONDS);
    assertEquals(0, counter[0]);
    assertEquals(0, keyLock.getUsages(key));
  }

  @Test
  public void testTryLock() throws InterruptedException {
    CountDownLatch latch = new CountDownLatch(40);
    int[] counter = new int[1];
    // Increment the counter.
    startThreads(latch, getLocker(true), 20, 100, counter, true);
    // Decrement the counter.
    startThreads(latch, getLocker(true), 20, 100, counter, false);
    // Wait for all threads to exit.
    latch.await(30, TimeUnit.SECONDS);
    // Verify the count gets reset to 0.
    assertEquals(0, counter[0]);
    assertEquals(0, keyLock.getUsages(key));
  }

  private static BiConsumer<KeyLock<UUID>, UUID> getLocker(boolean tryLock) {
    return (keyLock, uuid) -> {
      if (tryLock) {
        while (!keyLock.tryLock(uuid)) {
          try {
            Thread.sleep(10);
          } catch (InterruptedException e) {
            throw new RuntimeException(e);
          }
        }
      } else {
        keyLock.acquireLock(uuid);
      }
    };
  }

  // Start threads which perform increment decrement operations concurrently.
  private void startThreads(
      CountDownLatch latch,
      BiConsumer<KeyLock<UUID>, UUID> locker,
      int threadCount,
      int writesPerThread,
      int[] counter,
      boolean increment) {
    for (int i = 0; i < threadCount; i++) {
      Thread thread =
          new Thread(
              () -> {
                try {
                  for (int j = 0; j < threadCount; j++) {
                    locker.accept(keyLock, key);
                    try {
                      if (increment) {
                        counter[0]++;
                      } else {
                        counter[0]--;
                      }
                      Thread.sleep(10);
                    } catch (InterruptedException e) {
                      throw new RuntimeException(e);
                    } finally {
                      keyLock.releaseLock(key);
                    }
                  }
                } finally {
                  latch.countDown();
                }
              });
      thread.start();
    }
  }
}
