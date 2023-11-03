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

package org.yb.util.json;

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import com.google.common.collect.Range;

public final class Checkers {
  public static <T extends Comparable<?>> ValueChecker<T> range(Class<T> clazz, Range<T> value) {
    return new RangeChecker<>(clazz, value);
  }

  // String
  public static ValueChecker<String> equal(String value) {
    return range(String.class, Range.closed(value, value));
  }

  // Boolean
  public static ValueChecker<Boolean> equal(boolean value) {
    return range(Boolean.class, Range.closed(value, value));
  }

  // Long
  private static ValueChecker<Long> longChecker(Range<Long> value) {
    return range(Long.class, value);
  }

  public static ValueChecker<Long> less(long value) {
    return longChecker(Range.lessThan(value));
  }

  public static ValueChecker<Long> equal(long value) {
    return longChecker(Range.closed(value, value));
  }

  public static ValueChecker<Long> greater(long value) {
    return longChecker(Range.greaterThan(value));
  }

  public static ValueChecker<Long> lessOrEqual(long value) {
    return longChecker(Range.atMost(value));
  }

  public static ValueChecker<Long> greaterOrEqual(long value) {
    return longChecker(Range.atLeast(value));
  }

  // Double
  private static ValueChecker<Double> doubleChecker(Range<Double> value) {
    return range(Double.class, value);
  }

  public static ValueChecker<Double> less(double value) {
    return doubleChecker(Range.lessThan(value));
  }

  public static ValueChecker<Double> equal(double value) {
    return equal(value, 1e-8);
  }

  public static ValueChecker<Double> equal(double value, double epsilon) {
    return doubleChecker(Range.closed(value - epsilon, value + epsilon));
  }

  public static ValueChecker<Double> greater(double value) {
    return doubleChecker(Range.greaterThan(value));
  }

  public static ValueChecker<Double> lessOrEqual(double value) {
    return doubleChecker(Range.atMost(value));
  }

  public static ValueChecker<Double> greaterOrEqual(double value) {
    return doubleChecker(Range.atLeast(value));
  }

  // Array
  public static ArrayChecker array(List<Checker> checkers) {
    return new BasicArrayChecker(checkers);
  }

  public static ArrayChecker array(Checker... checkers) {
    return array(Arrays.asList(checkers));
  }

  // Object
  public static ObjectChecker object(Map<String, Checker> checkers) {
    return new BasicObjectChecker(checkers);
  }
}
