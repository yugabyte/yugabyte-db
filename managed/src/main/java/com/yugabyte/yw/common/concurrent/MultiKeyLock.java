/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.concurrent;

import java.util.Collection;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;

public class MultiKeyLock<T> extends KeyLock<T> {
  private final Comparator<T> comparator;

  public MultiKeyLock(Comparator<T> comparator) {
    this.comparator = comparator;
  }

  public void acquireLocks(Collection<T> keys) {
    List<T> sortedKeys =
        keys.stream()
            .filter(Objects::nonNull)
            .sorted(comparator)
            .distinct()
            .collect(Collectors.toList());
    sortedKeys.forEach(this::acquireLock);
  }

  public void releaseLocks(Collection<T> keys) {
    List<T> sortedKeys =
        keys.stream()
            .filter(Objects::nonNull)
            .sorted(comparator)
            .distinct()
            .collect(Collectors.toList());
    sortedKeys.forEach(this::releaseLock);
  }
}
