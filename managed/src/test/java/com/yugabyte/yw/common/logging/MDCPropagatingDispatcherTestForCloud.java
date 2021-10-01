package com.yugabyte.yw.common.logging;

import akka.dispatch.Batchable;
import org.junit.Test;
import org.slf4j.MDC;
import scala.concurrent.ExecutionContextExecutor;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.assertEquals;

public class MDCPropagatingDispatcherTestForCloud extends MDCPropagatingDispatcherTestBase {
  public MDCPropagatingDispatcherTestForCloud() {
    super(true);
  }

  @Test
  public void testWithCloudEnabled() throws InterruptedException {
    String mdcAttrKey = "mdcAttrKey";
    String mdcAttrVal = "mdcAttrVal";
    MDC.put(mdcAttrKey, mdcAttrVal);

    ExecutionContextExecutor dispatcher = app.asScala().actorSystem().dispatcher();
    assertEquals(MDCPropagatingDispatcher.class, dispatcher.getClass());

    AtomicReference<AssertionError> error = new AtomicReference<>();
    CountDownLatch cdl = new CountDownLatch(1);
    AtomicReference<CountDownLatch> cdlRef = new AtomicReference<>();
    cdlRef.set(cdl);

    Runnable runnable =
        () -> {
          try {
            assertEquals(mdcAttrVal, MDC.get(mdcAttrKey));
          } catch (AssertionError e) {
            error.set(e);
          } finally {
            cdlRef.get().countDown();
          }
        };

    Batchable batchable =
        new Batchable() {
          @Override
          public boolean isBatchable() {
            return true;
          }

          @Override
          public void run() {
            runnable.run();
          }
        };

    dispatcher.prepare().execute(runnable);
    cdl.await();

    AssertionError e = error.get();
    if (e != null) {
      throw e;
    }

    cdl = new CountDownLatch(1);
    cdlRef.set(cdl);
    dispatcher.prepare().execute(batchable);
    cdl.await();

    e = error.get();
    if (e != null) {
      throw e;
    }
  }
}
