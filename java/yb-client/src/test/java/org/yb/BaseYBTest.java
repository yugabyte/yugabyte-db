/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb;

import org.junit.Rule;
import org.junit.rules.ExpectedException;
import org.junit.rules.TestRule;
import org.junit.rules.TestWatcher;
import org.junit.rules.Timeout;
import org.junit.runner.Description;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/**
 * Base class for all YB Java tests.
 */
public class BaseYBTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseYBTest.class);

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(
      getTestMethodTimeoutSec(), TimeUnit.SECONDS);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  @Rule
  public final TestRule watcher = new TestWatcher() {

    private Map<String, Long> startTimesMs = new HashMap<>();

    private final String descriptionToStr(Description description) {
      return TestUtils.getClassAndMethodStr(description);
    }

    @Override
    protected void starting(Description description) {
      // The format of the heading below is important as it is parsed by external test dashboarding
      // tools.
      String descStr = descriptionToStr(description);
      TestUtils.printHeading(System.out, "Starting YB Java test: " + descStr);
      startTimesMs.put(descStr, System.currentTimeMillis());
    }

    @Override
    protected void succeeded(Description description) {
      LOG.info("YB Java test succeeded: " + descriptionToStr(description));
      super.succeeded(description);
    }

    @Override
    protected void failed(Throwable e, Description description) {
      LOG.error("YB Java test failed: " + descriptionToStr(description), e);
      super.failed(e, description);
    }

    /**
     * Invoked when a test method finishes (whether passing or failing).
     */
    @Override
    protected void finished(Description description) {
      // The format of the heading below is important as it is parsed by external test dashboarding
      // tools.
      String descStr = TestUtils.getClassAndMethodStr(description);
      Long startTimeMs = startTimesMs.get(descStr);
      if (startTimeMs == null) {
        LOG.error("Could not identify start time for test " + descStr + " to compute its duration");
      } else {
        double elapsedTimeSec = (System.currentTimeMillis() - startTimeMs) / 1000.0;
        LOG.info("YB Java test " + descStr + String.format(" took %.2f seconds", elapsedTimeSec));
      }
      TestUtils.printHeading(System.out, "Finished YB Java test: " + descStr);
    }
  };

  /**
   * This can be used for subclasses to override each test method's timeout in milliseconds. This is
   * called from the constructor, so the state of the derived class might not be fully initialized.
   */
  public int getTestMethodTimeoutSec() {
    return 120;
  }

}
