/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.swagger;

import com.google.common.base.Strings;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YBAInternalProcessor {
  private String excludeYbaInternal;

  enum ExcludeKind {
    NONE,
    ALL,
  }

  private ExcludeKind excludeKind = ExcludeKind.NONE;

  /*
   * @param excludeExpression can be set to something like - "all", "none"
   */
  public YBAInternalProcessor(String excludeExpression) {
    setExcludeInternal(excludeExpression);
  }

  private void setExcludeInternal(String excludeExpression) {
    if (Strings.isNullOrEmpty(excludeExpression)
        || excludeExpression.trim().equalsIgnoreCase("all")) {
      excludeKind = ExcludeKind.ALL;
      log.info("Setting Internal to exclude ALL");
      return;
    }
    if (excludeExpression.trim().equalsIgnoreCase("none")) {
      excludeKind = ExcludeKind.NONE;
      log.info("Setting Internal to exclude NONE");
      return;
    }
    throwInvalidExcludeExpression(excludeExpression);
  }

  private void throwInvalidExcludeExpression(String excludeExpression) {
    String errMsg = String.format("--exclude-internal=%s is invalid", excludeExpression);
    throw new RuntimeException(errMsg);
  }

  public boolean shouldExcludeInternal() {
    switch (excludeKind) {
      case ALL:
        return true;
      case NONE:
        return false;
      default:
        return true; // exclude all internal APIs by default
    }
  }

  // For unit testing
  ExcludeKind getExcludeKind() {
    return excludeKind;
  }
}
