// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.Comparators;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import org.apache.pekko.actor.CoordinatedShutdown;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ShutdownHookHandlerTest {

  @Mock private CoordinatedShutdown mockCoordinatedShutdown;

  @Test
  public void testOnApplicationShutdown() {
    ShutdownHookHandler handler = new ShutdownHookHandler(mockCoordinatedShutdown);
    int weights[] = new int[] {1, 2, 1, 3, 1, 5, 2, 10, 7, 9, 5, 3};
    List<Integer> weightOrders = new ArrayList<>();
    List<Object> objects = new ArrayList<>();
    for (int i = 0; i < weights.length; i++) {
      int weight = weights[i];
      Object referent = new Object();
      objects.add(referent);
      handler.addShutdownHook(
          referent,
          (obj) -> {
            assertTrue(obj == referent);
            synchronized (weightOrders) {
              weightOrders.add(weight);
            }
          },
          weight);
    }
    // Invoke the hooks now.
    handler.onApplicationShutdown();
    assertEquals(weights.length, weightOrders.size());
    assertTrue(
        "Received weights" + weightOrders,
        Comparators.isInOrder(weightOrders, Comparator.reverseOrder()));
  }
}
