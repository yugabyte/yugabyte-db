package com.yugabyte.yw.common.logging;

import org.junit.Test;

import static org.junit.Assert.assertFalse;

public class MDCPropagatingDispatcherTest extends MDCPropagatingDispatcherTestBase {
  public MDCPropagatingDispatcherTest() {
    super(false);
  }

  @Test
  public void testWithCloudDisabled() {
    // test if in case yb.cloud.enabled = false we are not overriding the execution context
    assertFalse(
        MDCPropagatingDispatcher.MDCPropagatingExecutionContext.class.isAssignableFrom(
            app.asScala().actorSystem().dispatcher().prepare().getClass()));
  }
}
