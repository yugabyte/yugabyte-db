package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.*;

import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue.ResourceAction;
import com.yugabyte.yw.common.utils.Pair;
import java.util.ArrayList;
import java.util.Iterator;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

/** OperatorWorkQueueTest */
@RunWith(MockitoJUnitRunner.class)
public class OperatorWorkQueueTest {
  OperatorWorkQueue workQueue;

  @Before
  public void beforeTest() {
    workQueue = new OperatorWorkQueue(1, 1, 2);
  }

  @Test
  public void testAddPopOrder() {
    ArrayList<Pair<String, OperatorWorkQueue.ResourceAction>> order = new ArrayList<>();
    order.add(
        new Pair<String, OperatorWorkQueue.ResourceAction>(
            "one", OperatorWorkQueue.ResourceAction.CREATE));
    order.add(
        new Pair<String, OperatorWorkQueue.ResourceAction>(
            "one", OperatorWorkQueue.ResourceAction.UPDATE));
    order.add(
        new Pair<String, OperatorWorkQueue.ResourceAction>(
            "two", OperatorWorkQueue.ResourceAction.CREATE));
    order.add(
        new Pair<String, OperatorWorkQueue.ResourceAction>(
            "one", OperatorWorkQueue.ResourceAction.DELETE));
    order.add(
        new Pair<String, OperatorWorkQueue.ResourceAction>(
            "two", OperatorWorkQueue.ResourceAction.DELETE));

    Iterator<Pair<String, OperatorWorkQueue.ResourceAction>> iter = order.iterator();
    while (iter.hasNext()) {
      workQueue.add(iter.next());
    }

    int count = 0;
    while (!workQueue.isEmpty()) {
      assertTrue(count < order.size());
      Pair<String, OperatorWorkQueue.ResourceAction> item = workQueue.pop(0);
      assertEquals(item, order.get(count));
      count++;
    }
    assertEquals(count, order.size());
  }

  @Test
  public void testWaitForQueue() {
    Thread waitThread = new Thread(() -> this.workQueue.pop());
    waitThread.start();
    try {
      Thread.sleep(500);
      assertTrue(waitThread.isAlive()); // Thread should be alive and waiting on .pop
      workQueue.add(new Pair<>("item", OperatorWorkQueue.ResourceAction.CREATE));
      Thread.sleep(500);
      assertFalse(waitThread.isAlive()); // Thread should have ended by now, with pop returning
    } catch (InterruptedException e) {
      System.out.println("interrupted");
    }
  }

  @Test
  public void testRequeueOnUniverseLockRequeue() throws InterruptedException {
    workQueue.requeue("one", ResourceAction.CREATE, false);
    Thread.sleep(8500);
    // Create locks requeue so this UPDATE should not be added
    workQueue.requeue("one", ResourceAction.UPDATE, false);
    Thread.sleep(8500);
    int count = 0;
    while (!workQueue.isEmpty()) {
      workQueue.pop();
      count++;
    }
    // Create and NoOp
    // Not the second NoOp
    assertEquals(count, 2);

    // Should allow to add this element now
    workQueue.requeue("one", OperatorWorkQueue.ResourceAction.UPDATE, false);
    Thread.sleep(8500);
    // Not allowed to add, since Update puts NoOp action too, should lock NoOp
    // requeue.
    workQueue.requeue("one", OperatorWorkQueue.ResourceAction.NO_OP, false);
    Thread.sleep(3500);
    count = 0;
    while (!workQueue.isEmpty()) {
      workQueue.pop();
      count++;
    }
    assertEquals(count, 2);
  }
}
