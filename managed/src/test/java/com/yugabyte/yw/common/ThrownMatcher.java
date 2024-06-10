/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import static org.hamcrest.MatcherAssert.assertThat;

import org.hamcrest.Description;
import org.hamcrest.Matcher;
import org.hamcrest.TypeSafeMatcher;

public class ThrownMatcher extends TypeSafeMatcher<Runnable> {

  private static final String NOTHING = "nothing";

  private final String expected;
  private final String expectedMessage;
  private final Matcher<String> messageAssertion;
  private String actual;
  private String actualMessage;

  public ThrownMatcher(String s, String expectedMessage, Matcher<String> messageAssertion) {
    expected = s;
    this.expectedMessage = expectedMessage;
    this.messageAssertion = messageAssertion;
  }

  public static Matcher<Runnable> thrown(Class<? extends Throwable> expected) {
    return new ThrownMatcher(expected.getName(), null, null);
  }

  public static Matcher<Runnable> thrown(
      Class<? extends Throwable> expected, Matcher<String> messageAssertion) {
    return new ThrownMatcher(expected.getName(), null, messageAssertion);
  }

  public static Matcher<Runnable> thrown(
      Class<? extends Throwable> expected, String expectedMessage) {
    return new ThrownMatcher(expected.getName(), expectedMessage, null);
  }

  @Override
  public boolean matchesSafely(Runnable action) {
    actual = NOTHING;
    actualMessage = NOTHING;
    try {
      action.run();
      return false;
    } catch (Throwable t) {
      actual = t.getClass().getName();
      actualMessage = t.getMessage();
      return actual.equals(expected)
          && (expectedMessage == null || actualMessage.equals(expectedMessage))
          && assertMessage();
    }
  }

  private boolean assertMessage() {
    if (messageAssertion == null) {
      return true;
    }
    assertThat(actualMessage, messageAssertion);
    return true;
  }

  @Override
  public void describeTo(Description description) {
    if (!actual.equals(expected)) {
      description.appendText("throw " + expected);
    } else if (expectedMessage != null) {
      description.appendText("message '" + expectedMessage + "'");
    }
  }

  @Override
  protected void describeMismatchSafely(Runnable item, Description mismatchDescription) {
    if (!actual.equals(expected)) {
      mismatchDescription.appendText("threw " + actual);
    } else if (expectedMessage != null) {
      mismatchDescription.appendText("message '" + actualMessage + "'");
    }
  }
}
