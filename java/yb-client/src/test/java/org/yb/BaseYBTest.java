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
import org.yb.client.TestUtils;

import java.util.concurrent.TimeUnit;

/**
 * Base class for all YB Java tests.
 */
public class BaseYBTest {

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(
      getTestMethodTimeoutSec(), TimeUnit.SECONDS);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  @Rule
  public final TestRule watcher = new TestWatcher() {
    protected void starting(Description description) {
      TestUtils.printHeading(System.out,
          "Starting YB Java test: " + TestUtils.getClassAndMethodStr(description));
    }

    /**
     * Invoked when a test method finishes (whether passing or failing).
     */
    protected void finished(Description description) {
      TestUtils.printHeading(System.out,
          "Finished YB Java test: " + TestUtils.getClassAndMethodStr(description));
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
