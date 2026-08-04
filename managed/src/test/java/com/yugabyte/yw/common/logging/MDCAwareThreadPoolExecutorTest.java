package com.yugabyte.yw.common.logging;

import static org.junit.Assert.*;

import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import org.junit.Test;
import org.slf4j.MDC;

public class MDCAwareThreadPoolExecutorTest {
  @Test
  public void testMDCPropagation() throws InterruptedException {
    BlockingQueue<Runnable> queue = new LinkedBlockingQueue<>(1);

    ExecutorService executor =
        new MDCAwareThreadPoolExecutor(
            1, // core pool size
            1, // max pool size
            10,
            TimeUnit.SECONDS,
            queue,
            Executors.defaultThreadFactory());
    List<CountDownLatch> cdl =
        IntStream.of(0, 1).mapToObj(i -> new CountDownLatch(1)).collect(Collectors.toList());
    String mdcAttrKey = "mdcAttrKey";
    String mdcAttrVal = "mdcAttrVal";
    MDC.put(mdcAttrKey, mdcAttrVal);

    // make sure MDC attributes are properly set in the thread
    AtomicReference<AssertionError> error = new AtomicReference<>();
    executor.submit(
        () -> {
          try {
            assertEquals(mdcAttrVal, MDC.get(mdcAttrKey));
            cdl.get(0).countDown();
          } catch (AssertionError e) {
            error.set(e);
          } finally {
            cdl.get(0).countDown();
          }
        });

    cdl.get(0).await();

    try {
      if (error.get() != null) {
        throw error.get();
      }

      // make sure MDC is cleared
      MDC.clear();
      executor.submit(
          () -> {
            try {
              assertNull(MDC.get(mdcAttrKey));
              cdl.get(1).countDown();
            } catch (AssertionError e) {
              error.set(e);
            } finally {
              cdl.get(1).countDown();
            }
          });

      cdl.get(1).await();

      if (error.get() != null) {
        throw error.get();
      }
    } finally {
      executor.shutdown();
      executor.awaitTermination(5, TimeUnit.SECONDS);
    }
  }
}
