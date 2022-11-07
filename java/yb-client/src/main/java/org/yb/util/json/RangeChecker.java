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

import com.google.common.collect.BoundType;
import com.google.common.collect.Range;

final class RangeChecker<T extends Comparable<?>> extends ValueChecker<T> {
  private final Range<T> range;

  RangeChecker(Class<T> clazz, Range<T> range) {
    super(clazz);
    this.range = range;
  }

  @Override
  protected boolean checkValue(T value, ConflictCollector collector) {
    if (range.contains(value)) {
      return true;
    }
    collector.addConflict(String.format(
        "'%s' is not %s",  value.toString(), describeRange()));
    return false;
  }

  private String describeRange() {
    T upper = null;
    T lower = null;
    boolean isLowerClosed = false;
    boolean isUpperClosed = false;
    if (range.hasLowerBound()) {
      lower = range.lowerEndpoint();
      isLowerClosed = range.lowerBoundType() == BoundType.CLOSED;
    }
    if (range.hasUpperBound()) {
      upper = range.upperEndpoint();
      isUpperClosed = range.upperBoundType() == BoundType.CLOSED;
    }
    if (lower != null && upper != null) {
      if (isLowerClosed && isUpperClosed && lower.equals(upper)) {
        return String.format("== '%s'", lower.toString());
      }
      return String.format(
          "in '%c%s, %s%c'",
          isLowerClosed ? '[' : '(', lower.toString(),
          upper.toString(), isUpperClosed ? ']' : ')');
    }

    T value = lower;
    boolean isClosed = isLowerClosed;
    char comparingSymbol = '>';
    if (value == null) {
      value = upper;
      isClosed = isUpperClosed;
      comparingSymbol = '<';
    }
    assert value != null;
    @SuppressWarnings("null") String asString = value.toString();
    return String.format(isClosed ? "%c= '%s'" : "%c '%s'", comparingSymbol, asString);
  }
}
