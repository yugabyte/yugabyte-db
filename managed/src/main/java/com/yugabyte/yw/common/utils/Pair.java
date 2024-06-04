/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.utils;

import lombok.Data;

@Data
public class Pair<U, V> {
  private final U first;
  private final V second;

  public Pair(U first, V second) {
    this.first = first;
    this.second = second;
  }
}
