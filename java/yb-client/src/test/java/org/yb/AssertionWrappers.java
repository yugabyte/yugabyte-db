// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb;

import org.hamcrest.Matcher;
import org.junit.Assert;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A set of assertion methods useful for writing tests. Only failed assertions
 * are recorded. These methods can be used directly:
 * <code>Assert.assertEquals(...)</code>, however, they read better if they
 * are referenced through static import:
 *
 * <pre>
 * import static org.yb.AssertionWrappers.*;
 *    ...
 *    assertEquals(...);
 * </pre>
 *
 * @see AssertionError
 * @since 4.0
 */
public class AssertionWrappers {

  private static final Logger LOG = LoggerFactory.getLogger(AssertionWrappers.class);

  private static void logError(Error error) {
    LOG.error("Assertion error:", error);
  }

  private static void wrapAssertion(Runnable fn) {
    try {
      fn.run();
    } catch (Error error) {
      logError(error);
      throw error;
    }
  }

  /**
   * Asserts that a condition is true. If it isn't it throws an
   * {@link AssertionError} with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param condition condition to be checked
   */
  public static void assertTrue(String message, boolean condition) {
    wrapAssertion(() -> Assert.assertTrue(message, condition));
  }

  /**
   * Asserts that a condition is true. If it isn't it throws an
   * {@link AssertionError} without a message.
   *
   * @param condition condition to be checked
   */
  public static void assertTrue(boolean condition) {
    wrapAssertion(() -> Assert.assertTrue(condition));
  }

  /**
   * Asserts that a condition is false. If it isn't it throws an
   * {@link AssertionError} with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param condition condition to be checked
   */
  public static void assertFalse(String message, boolean condition) {
    wrapAssertion(() -> Assert.assertFalse(message, condition));
  }

  /**
   * Asserts that a condition is false. If it isn't it throws an
   * {@link AssertionError} without a message.
   *
   * @param condition condition to be checked
   */
  public static void assertFalse(boolean condition) {
    wrapAssertion(() -> Assert.assertFalse(condition));
  }

  /**
   * Fails a test with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @see AssertionError
   */
  public static void fail(String message) {
    wrapAssertion(() -> Assert.fail(message));
  }

  /**
   * Fails a test with no message.
   */
  public static void fail() {
    wrapAssertion(() -> Assert.fail(null));
  }

  /**
   * Asserts that two objects are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message. If
   * <code>expected</code> and <code>actual</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expected expected value
   * @param actual actual value
   */
  public static void assertEquals(String message, Object expected,
                                  Object actual) {
    wrapAssertion(() -> Assert.assertEquals(message, expected, actual));
  }

  /**
   * Asserts that two objects are equal. If they are not, an
   * {@link AssertionError} without a message is thrown. If
   * <code>expected</code> and <code>actual</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param expected expected value
   * @param actual the value to check against <code>expected</code>
   */
  public static void assertEquals(Object expected, Object actual) {
    wrapAssertion(() -> Assert.assertEquals(expected, actual));
  }

  /**
   * Asserts that actual satisfies the condition specified by matcher.
   * If not, an AssertionError is thrown with information about the
   * matcher and failing value.
   *
   * Type Parameters:
   * T - the static type accepted by the matcher
   * Parameters:
   * @param actual - the computed value being compared
   * @param matcher - an expression, built of Matchers, specifying allowed values
   */
  public static <T> void assertThat(T actual, Matcher<? super T> matcher) {
    wrapAssertion(() -> Assert.assertThat(actual, matcher));
  }

  /**
   * Asserts that two objects are <b>not</b> equals. If they are, an
   * {@link AssertionError} is thrown with the given message. If
   * <code>unexpected</code> and <code>actual</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param unexpected unexpected value to check
   * @param actual the value to check against <code>unexpected</code>
   */
  public static void assertNotEquals(String message, Object unexpected,
                                     Object actual) {
    wrapAssertion(() -> Assert.assertNotEquals(message, unexpected, actual));
  }

  /**
   * Asserts that two objects are <b>not</b> equals. If they are, an
   * {@link AssertionError} without a message is thrown. If
   * <code>unexpected</code> and <code>actual</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param unexpected unexpected value to check
   * @param actual the value to check against <code>unexpected</code>
   */
  public static void assertNotEquals(Object unexpected, Object actual) {
    wrapAssertion(() -> Assert.assertNotEquals(unexpected, actual));
  }

  /**
   * Asserts that two longs are <b>not</b> equals. If they are, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param unexpected unexpected value to check
   * @param actual the value to check against <code>unexpected</code>
   */
  public static void assertNotEquals(String message, long unexpected, long actual) {
    wrapAssertion(() -> Assert.assertNotEquals(message, unexpected, actual));
  }

  /**
   * Asserts that two longs are <b>not</b> equals. If they are, an
   * {@link AssertionError} without a message is thrown.
   *
   * @param unexpected unexpected value to check
   * @param actual the value to check against <code>unexpected</code>
   */
  public static void assertNotEquals(long unexpected, long actual) {
    wrapAssertion(() -> Assert.assertNotEquals(unexpected, actual));
  }

  /**
   * Asserts that two doubles are <b>not</b> equal to within a positive delta.
   * If they are, an {@link AssertionError} is thrown with the given
   * message. If the unexpected value is infinity then the delta value is
   * ignored. NaNs are considered equal:
   * <code>assertNotEquals(Double.NaN, Double.NaN, *)</code> fails
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param unexpected unexpected value
   * @param actual the value to check against <code>unexpected</code>
   * @param delta the maximum delta between <code>unexpected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertNotEquals(String message, double unexpected,
                                     double actual, double delta) {
    wrapAssertion(() -> Assert.assertNotEquals(message, unexpected, actual, delta));
  }

  /**
   * Asserts that two doubles are <b>not</b> equal to within a positive delta.
   * If they are, an {@link AssertionError} is thrown. If the unexpected
   * value is infinity then the delta value is ignored.NaNs are considered
   * equal: <code>assertNotEquals(Double.NaN, Double.NaN, *)</code> fails
   *
   * @param unexpected unexpected value
   * @param actual the value to check against <code>unexpected</code>
   * @param delta the maximum delta between <code>unexpected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertNotEquals(double unexpected, double actual, double delta) {
    wrapAssertion(() -> Assert.assertNotEquals(unexpected, actual, delta));
  }

  /**
   * Asserts that two floats are <b>not</b> equal to within a positive delta.
   * If they are, an {@link AssertionError} is thrown. If the unexpected
   * value is infinity then the delta value is ignored.NaNs are considered
   * equal: <code>assertNotEquals(Float.NaN, Float.NaN, *)</code> fails
   *
   * @param unexpected unexpected value
   * @param actual the value to check against <code>unexpected</code>
   * @param delta the maximum delta between <code>unexpected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertNotEquals(float unexpected, float actual, float delta) {
    wrapAssertion(() -> Assert.assertNotEquals(null, unexpected, actual, delta));
  }

  /**
   * Asserts that two object arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message. If
   * <code>expecteds</code> and <code>actuals</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds Object array or array of arrays (multi-dimensional array) with
   * expected values.
   * @param actuals Object array or array of arrays (multi-dimensional array) with
   * actual values
   */
  public static void assertArrayEquals(String message, Object[] expecteds,
                                       Object[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two object arrays are equal. If they are not, an
   * {@link AssertionError} is thrown. If <code>expected</code> and
   * <code>actual</code> are <code>null</code>, they are considered
   * equal.
   *
   * @param expecteds Object array or array of arrays (multi-dimensional array) with
   * expected values
   * @param actuals Object array or array of arrays (multi-dimensional array) with
   * actual values
   */
  public static void assertArrayEquals(Object[] expecteds, Object[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two boolean arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message. If
   * <code>expecteds</code> and <code>actuals</code> are <code>null</code>,
   * they are considered equal.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds boolean array with expected values.
   * @param actuals boolean array with expected values.
   */
  public static void assertArrayEquals(String message, boolean[] expecteds,
                                       boolean[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two boolean arrays are equal. If they are not, an
   * {@link AssertionError} is thrown. If <code>expected</code> and
   * <code>actual</code> are <code>null</code>, they are considered
   * equal.
   *
   * @param expecteds boolean array with expected values.
   * @param actuals boolean array with expected values.
   */
  public static void assertArrayEquals(boolean[] expecteds, boolean[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two byte arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds byte array with expected values.
   * @param actuals byte array with actual values
   */
  public static void assertArrayEquals(String message, byte[] expecteds,
                                       byte[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two byte arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds byte array with expected values.
   * @param actuals byte array with actual values
   */
  public static void assertArrayEquals(byte[] expecteds, byte[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two char arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds char array with expected values.
   * @param actuals char array with actual values
   */
  public static void assertArrayEquals(String message, char[] expecteds,
                                       char[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two char arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds char array with expected values.
   * @param actuals char array with actual values
   */
  public static void assertArrayEquals(char[] expecteds, char[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two short arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds short array with expected values.
   * @param actuals short array with actual values
   */
  public static void assertArrayEquals(String message, short[] expecteds,
                                       short[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two short arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds short array with expected values.
   * @param actuals short array with actual values
   */
  public static void assertArrayEquals(short[] expecteds, short[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two int arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds int array with expected values.
   * @param actuals int array with actual values
   */
  public static void assertArrayEquals(String message, int[] expecteds,
                                       int[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two int arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds int array with expected values.
   * @param actuals int array with actual values
   */
  public static void assertArrayEquals(int[] expecteds, int[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two long arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds long array with expected values.
   * @param actuals long array with actual values
   */
  public static void assertArrayEquals(String message, long[] expecteds,
                                       long[] actuals) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals));
  }

  /**
   * Asserts that two long arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds long array with expected values.
   * @param actuals long array with actual values
   */
  public static void assertArrayEquals(long[] expecteds, long[] actuals) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals));
  }

  /**
   * Asserts that two double arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds double array with expected values.
   * @param actuals double array with actual values
   * @param delta the maximum delta between <code>expecteds[i]</code> and
   * <code>actuals[i]</code> for which both numbers are still
   * considered equal.
   */
  public static void assertArrayEquals(String message, double[] expecteds,
                                       double[] actuals, double delta) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals, delta));
  }

  /**
   * Asserts that two double arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds double array with expected values.
   * @param actuals double array with actual values
   * @param delta the maximum delta between <code>expecteds[i]</code> and
   * <code>actuals[i]</code> for which both numbers are still
   * considered equal.
   */
  public static void assertArrayEquals(double[] expecteds, double[] actuals, double delta) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals, delta));
  }

  /**
   * Asserts that two float arrays are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expecteds float array with expected values.
   * @param actuals float array with actual values
   * @param delta the maximum delta between <code>expecteds[i]</code> and
   * <code>actuals[i]</code> for which both numbers are still
   * considered equal.
   */
  public static void assertArrayEquals(String message, float[] expecteds,
                                       float[] actuals, float delta) throws AssertionError {
    wrapAssertion(() -> Assert.assertArrayEquals(message, expecteds, actuals, delta));
  }

  /**
   * Asserts that two float arrays are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expecteds float array with expected values.
   * @param actuals float array with actual values
   * @param delta the maximum delta between <code>expecteds[i]</code> and
   * <code>actuals[i]</code> for which both numbers are still
   * considered equal.
   */
  public static void assertArrayEquals(float[] expecteds, float[] actuals, float delta) {
    wrapAssertion(() -> Assert.assertArrayEquals(expecteds, actuals, delta));
  }

  /**
   * Asserts that two doubles are equal to within a positive delta.
   * If they are not, an {@link AssertionError} is thrown with the given
   * message. If the expected value is infinity then the delta value is
   * ignored. NaNs are considered equal:
   * <code>assertEquals(Double.NaN, Double.NaN, *)</code> passes
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expected expected value
   * @param actual the value to check against <code>expected</code>
   * @param delta the maximum delta between <code>expected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertEquals(String message, double expected,
                                  double actual, double delta) {
    wrapAssertion(() -> Assert.assertEquals(message, expected, actual, delta));
  }

  /**
   * Asserts that two floats are equal to within a positive delta.
   * If they are not, an {@link AssertionError} is thrown with the given
   * message. If the expected value is infinity then the delta value is
   * ignored. NaNs are considered equal:
   * <code>assertEquals(Float.NaN, Float.NaN, *)</code> passes
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expected expected value
   * @param actual the value to check against <code>expected</code>
   * @param delta the maximum delta between <code>expected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertEquals(String message, float expected,
                                  float actual, float delta) {
    wrapAssertion(() -> Assert.assertEquals(message, expected, actual, delta));
  }

  /**
   * Asserts that two floats are <b>not</b> equal to within a positive delta.
   * If they are, an {@link AssertionError} is thrown with the given
   * message. If the unexpected value is infinity then the delta value is
   * ignored. NaNs are considered equal:
   * <code>assertNotEquals(Float.NaN, Float.NaN, *)</code> fails
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param unexpected unexpected value
   * @param actual the value to check against <code>unexpected</code>
   * @param delta the maximum delta between <code>unexpected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertNotEquals(String message, float unexpected,
                                     float actual, float delta) {
    wrapAssertion(() -> Assert.assertNotEquals(message, unexpected, actual, delta));
  }

  /**
   * Asserts that two longs are equal. If they are not, an
   * {@link AssertionError} is thrown.
   *
   * @param expected expected long value.
   * @param actual actual long value
   */
  public static void assertEquals(long expected, long actual) {
    wrapAssertion(() -> Assert.assertEquals(expected, actual));
  }

  /**
   * Asserts that two longs are equal. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expected long expected value.
   * @param actual long actual value
   */
  public static void assertEquals(String message, long expected, long actual) {
    wrapAssertion(() -> Assert.assertEquals(message, expected, actual));
  }

  /**
   * Asserts that two doubles are equal to within a positive delta.
   * If they are not, an {@link AssertionError} is thrown. If the expected
   * value is infinity then the delta value is ignored.NaNs are considered
   * equal: <code>assertEquals(Double.NaN, Double.NaN, *)</code> passes
   *
   * @param expected expected value
   * @param actual the value to check against <code>expected</code>
   * @param delta the maximum delta between <code>expected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertEquals(double expected, double actual, double delta) {
    wrapAssertion(() -> Assert.assertEquals(expected, actual, delta));
  }

  /**
   * Asserts that two floats are equal to within a positive delta.
   * If they are not, an {@link AssertionError} is thrown. If the expected
   * value is infinity then the delta value is ignored. NaNs are considered
   * equal: <code>assertEquals(Float.NaN, Float.NaN, *)</code> passes
   *
   * @param expected expected value
   * @param actual the value to check against <code>expected</code>
   * @param delta the maximum delta between <code>expected</code> and
   * <code>actual</code> for which both numbers are still
   * considered equal.
   */
  public static void assertEquals(float expected, float actual, float delta) {
    wrapAssertion(() -> Assert.assertEquals(expected, actual, delta));
  }

  /**
   * Asserts that an object isn't null. If it is an {@link AssertionError} is
   * thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param object Object to check or <code>null</code>
   */
  public static void assertNotNull(String message, Object object) {
    wrapAssertion(() -> Assert.assertNotNull(message, object));
  }

  /**
   * Asserts that an object isn't null. If it is an {@link AssertionError} is
   * thrown.
   *
   * @param object Object to check or <code>null</code>
   */
  public static void assertNotNull(Object object) {
    wrapAssertion(() -> Assert.assertNotNull(object));
  }

  /**
   * Asserts that an object is null. If it is not, an {@link AssertionError}
   * is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param object Object to check or <code>null</code>
   */
  public static void assertNull(String message, Object object) {
    wrapAssertion(() -> Assert.assertNull(message, object));
  }

  /**
   * Asserts that an object is null. If it isn't an {@link AssertionError} is
   * thrown.
   *
   * @param object Object to check or <code>null</code>
   */
  public static void assertNull(Object object) {
    wrapAssertion(() -> Assert.assertNull(object));
  }

  /**
   * Asserts that two objects refer to the same object. If they are not, an
   * {@link AssertionError} is thrown with the given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param expected the expected object
   * @param actual the object to compare to <code>expected</code>
   */
  public static void assertSame(String message, Object expected, Object actual) {
    wrapAssertion(() -> Assert.assertSame(message, expected, actual));
  }

  /**
   * Asserts that two objects refer to the same object. If they are not the
   * same, an {@link AssertionError} without a message is thrown.
   *
   * @param expected the expected object
   * @param actual the object to compare to <code>expected</code>
   */
  public static void assertSame(Object expected, Object actual) {
    wrapAssertion(() -> Assert.assertSame(expected, actual));
  }

  /**
   * Asserts that two objects do not refer to the same object. If they do
   * refer to the same object, an {@link AssertionError} is thrown with the
   * given message.
   *
   * @param message the identifying message for the {@link AssertionError} (<code>null</code>
   * okay)
   * @param unexpected the object you don't expect
   * @param actual the object to compare to <code>unexpected</code>
   */
  public static void assertNotSame(String message, Object unexpected,
                                   Object actual) {
    wrapAssertion(() -> Assert.assertNotSame(message, unexpected, actual));
  }

  /**
   * Asserts that two objects do not refer to the same object. If they do
   * refer to the same object, an {@link AssertionError} without a message is
   * thrown.
   *
   * @param unexpected the object you don't expect
   * @param actual the object to compare to <code>unexpected</code>
   */
  public static void assertNotSame(Object unexpected, Object actual) {
    wrapAssertion(() -> Assert.assertNotSame(unexpected, actual));
  }

  /**
   * Asserts that the first parameter is greater than the second parameter. If not,
   * an {@link AssertionError} is thrown.
   *
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertGreaterThan(Comparable<T> a, T b) {
    assertGreaterThan(
        String.format("Expected %s to be greater than %s", a, b),
        a, b
    );
  }

  /**
   * Asserts that the first parameter is greater than or equal to the second parameter.
   * If not, an {@link AssertionError} is thrown.
   *
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertGreaterThanOrEqualTo(Comparable<T> a, T b) {
    assertGreaterThanOrEqualTo(
        String.format("Expected %s to be greater than or equal to %s", a, b),
        a, b
    );
  }

  /**
   * Asserts that the first parameter is less than the second parameter. If not,
   * an {@link AssertionError} is thrown.
   *
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertLessThan(Comparable<T> a, T b) {
    assertLessThan(
        String.format("Expected %s to be less than %s", a, b),
        a, b
    );
  }

  /**
   * Asserts that the first parameter is less than or equal to the second parameter.
   * If not, an {@link AssertionError} is thrown.
   *
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertLessThanOrEqualTo(Comparable<T> a, T b) {
    assertLessThanOrEqualTo(
        String.format("Expected %s to be less than or equal to %s", a, b),
        a, b
    );
  }

  /**
   * Asserts that the first parameter is greater than the second parameter. If not,
   * an {@link AssertionError} with the passed message is thrown.
   *
   * @param message the message to use in the {@link AssertionError}
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertGreaterThan(String message, Comparable<T> a, T b) {
    wrapAssertion(() -> Assert.assertTrue(message, a.compareTo(b) > 0));
  }

  /**
   * Asserts that the first parameter is greater than or equal to the second parameter.
   * If not, an {@link AssertionError} with the passed message is thrown.
   *
   * @param message the message to use in the {@link AssertionError}
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertGreaterThanOrEqualTo(String message, Comparable<T> a, T b) {
    wrapAssertion(() -> Assert.assertTrue(message, a.compareTo(b) >= 0));
  }

  /**
   * Asserts that the first parameter is less than the second parameter. If not,
   * an {@link AssertionError} with the passed message is thrown.
   *
   * @param message the message to use in the {@link AssertionError}
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertLessThan(String message, Comparable<T> a, T b) {
    wrapAssertion(() -> Assert.assertTrue(message, a.compareTo(b) < 0));
  }

  /**
   * Asserts that the first parameter is less than or equal to the second parameter.
   * If not, an {@link AssertionError} with the passed message is thrown.
   *
   * @param message the message to use in the {@link AssertionError}
   * @param a the object to compare
   * @param b the object being compared against
   */
  public static <T> void assertLessThanOrEqualTo(String message, Comparable<T> a, T b) {
    wrapAssertion(() -> Assert.assertTrue(message, a.compareTo(b) <= 0));
  }
}
