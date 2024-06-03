package com.yugabyte.yw.common.logging;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;
import org.apache.pekko.dispatch.Batchable;
import org.junit.Test;
import org.slf4j.MDC;
import scala.concurrent.ExecutionContextExecutor;

public class MDCPropagatingDispatcherTest extends MDCPropagatingDispatcherTestBase {
  public MDCPropagatingDispatcherTest() {
    super();
  }

  @Test
  public void testIfExecutionContextOverridden() throws InterruptedException {
    /*For both cloud and non-cloud deployments, the execution context should
     * be overridden to propagate the MDC. */
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

    dispatcher.execute(runnable);
    cdl.await();
    assertNull(error.get());

    cdl = new CountDownLatch(1);
    cdlRef.set(cdl);
    dispatcher.execute(batchable);
    cdl.await();
    assertNull(error.get());

    cdl = new CountDownLatch(1);
    cdlRef.set(cdl);
    dispatcher.prepare().execute(runnable);
    cdl.await();
    assertNull(error.get());

    cdl = new CountDownLatch(1);
    cdlRef.set(cdl);
    dispatcher.prepare().execute(batchable);
    cdl.await();
    assertNull(error.get());
  }
}
