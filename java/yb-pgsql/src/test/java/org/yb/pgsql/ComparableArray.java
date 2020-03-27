package org.yb.pgsql;

import java.util.Arrays;
import java.util.stream.Collectors;

/**
 * Helper class for comparing Postgres Array types. Non-equality comparisons of this class
 * are not meaningful: {@link ComparableArray#compareTo(Object)} does not define an ordering.
 */
class ComparableArray implements Comparable {
  private Comparable[] array;

  ComparableArray(Comparable... array) {
    this.array = array;
  }

  static ComparableArray arrayOf(Comparable... array) {
    return new ComparableArray(array);
  }

  @Override
  public boolean equals(Object o) {
    return compareTo(o) == 0;
  }

  /**
   * We are abusing the {@link Comparable} interface to provide row equality checking.
   * TODO write a better interface for row elements which does not require Comparable.
   */
  @Override
  public int compareTo(Object o) {
    if (!(o instanceof ComparableArray)) {
      return -1;
    }

    Comparable[] other = ((ComparableArray) o).array;

    if (array.length != other.length) {
      return -1;
    }

    for (int i = 0; i < array.length; i++) {
      if (array[i] == null || other[i] == null) {
        if (array[i] != other[i]) {
          return -1;
        }
      } else {
        int compare_result = array[i].compareTo(other[i]);
        if (compare_result != 0) {
          return compare_result;
        }
      }
    }
    return 0;
  }

  @Override
  public String toString() {
    String elements = Arrays.stream(array)
        .map(Object::toString)
        .collect(Collectors.joining(", "));

    return String.format("[%s]", elements);
  }
}
