/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb;

import org.junit.Rule;
import org.junit.rules.ExpectedException;
import org.junit.rules.Timeout;

/**
 * Base class for all YB Java tests.
 */
public class BaseYBTest {

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(getTestMethodTimeoutSec() * 1000);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  /**
   * This can be used for subclasses to override each test method's timeout in milliseconds. This is
   * called from the constructor, so the state of the derived class might not be fully initialized.
   */
  public int getTestMethodTimeoutSec() {
    return 120;
  }

}
