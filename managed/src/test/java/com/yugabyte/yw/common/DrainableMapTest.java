// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.junit.Test;

public class DrainableMapTest {

  @Test
  public void testEmpty() throws InterruptedException {
    DrainableMap<String, String> map = new DrainableMap<>();
    map.sealMap();
    // put must fail after the write is closed.
    assertThrows(
        IllegalStateException.class,
        () -> {
          map.put("k1", "v1");
        });
    boolean isDone = map.waitForEmpty(Duration.ofSeconds(2));
    assertEquals(true, isDone);
  }

  @Test
  public void testSealMap() throws InterruptedException, ExecutionException {
    DrainableMap<String, String> map = new DrainableMap<>();
    int size = 10;
    for (int i = 0; i < size; i++) {
      map.put("key" + i, "val" + i);
    }
    map.sealMap();
    // put must fail after the write is closed.
    assertThrows(
        IllegalStateException.class,
        () -> {
          map.put("k1", "v1");
        });
    boolean isDone = map.waitForEmpty(Duration.ofSeconds(2));
    assertEquals(false, isDone);
    ExecutorService executor = Executors.newFixedThreadPool(2);
    Future<Boolean> future =
        executor.submit(
            () -> {
              return map.waitForEmpty(Duration.ofSeconds(5));
            });
    executor
        .submit(
            () -> {
              for (int i = 0; i < size; i++) {
                map.remove("key" + i);
              }
            })
        .get();
    isDone = future.get();
    assertEquals(true, isDone);
    assertEquals(0, map.size());
  }
}
