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
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.common.YBADeprecated;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class YBADeprecationProcessor {
  private String excludeYbaDeprecated;

  enum ExcludeKind {
    NONE,
    ALL,
    SINCE_DATE,
    SINCE_VERSION
  }

  private ExcludeKind excludeKind = ExcludeKind.NONE;
  // excludeSinceDate is used when excludeKind is SINCE_DATE
  private Date excludeSinceDate;
  // excludeSinceVersion is used when excludeKind is SINCE_VERSION
  private String excludeSinceVersion;

  /*
   * @param excludeExpression can be set to something like - "all", "24m",
   * "2.17.1.0"
   */
  public YBADeprecationProcessor(String excludeExpression) {
    setExcludeDeprecated(excludeExpression);
  }

  private void setExcludeDeprecated(String excludeExpression) {
    if (Strings.isNullOrEmpty(excludeExpression)) {
      excludeKind = ExcludeKind.NONE;
      log.info("Setting Deprecation to exclude NONE");
      return;
    }
    excludeExpression = excludeExpression.trim();
    if (excludeExpression.equalsIgnoreCase("all")) {
      excludeKind = ExcludeKind.ALL;
      log.info("Setting Deprecation to exclude ALL");
      return;
    } else if (excludeExpression.endsWith("m")) {
      // treated like since months "24m"
      int nMonths = 0;
      try {
        nMonths = Integer.parseInt(excludeExpression.replaceFirst("m", ""));
      } catch (NumberFormatException e) {
        log.error("Expecting exclude expression in the format 'Nm' where N is a number", e);
        throwInvalidExcludeExpression(excludeExpression);
      }
      // set sinceDate to nMonths before now
      Calendar cutOffDate = Calendar.getInstance();
      cutOffDate.add(Calendar.MONTH, -nMonths);
      excludeSinceDate = cutOffDate.getTime();
      excludeKind = ExcludeKind.SINCE_DATE;
      log.info("Setting Deprecation to exclude since date {}", excludeSinceDate);
      return;
    } else if (Util.isYbVersionFormatValid(excludeExpression)) {
      // treated like a version "2.17.1.0"
      excludeKind = ExcludeKind.SINCE_VERSION;
      excludeSinceVersion = excludeExpression;
      log.info("Setting Deprecation to exclude since version {}", excludeSinceVersion);
      return;
    } else {
      // treat is as a date parseable by SimpleDateFormat
      try {
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd");
        excludeSinceDate = f.parse(excludeExpression);
        excludeKind = ExcludeKind.SINCE_DATE;
        log.info("Setting Deprecation to exclude since date {}", excludeSinceDate);
        return;
      } catch (ParseException e) {
        log.warn("Not able to parse {} as date string", excludeExpression);
      }
    }
    throwInvalidExcludeExpression(excludeExpression);
  }

  private void throwInvalidExcludeExpression(String excludeExpression) {
    String errMsg = String.format("--exclude-deprecated=%s is invalid", excludeExpression);
    throw new RuntimeException(errMsg);
  }

  // False if "DeprecatedSince: <value>" is before 'excludeSinceDate' (or)
  // "DeprecatedYBAVersion: <version>" is before 'excludeSinceVersion'
  // in the given api string, True otherwise.
  public boolean shouldExcludeDeprecated(YBADeprecated ybaDeprecated) {
    switch (excludeKind) {
      case ALL:
        return true;
      case NONE:
        return false;
      case SINCE_DATE:
        return shouldExcludeSinceDate(ybaDeprecated.sinceDate());
      case SINCE_VERSION:
        return shouldExcludeSinceVersion(ybaDeprecated.sinceYBAVersion());
      default:
        return false;
    }
  }

  private boolean shouldExcludeSinceDate(String sinceDate) {
    sinceDate = sinceDate.trim();
    // find api deprecated since date
    Date deprecatedSince;
    try {
      SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd");
      f.setLenient(false);
      deprecatedSince = f.parse(sinceDate);
    } catch (ParseException e) {
      String msg =
          String.format(
              "Deprecated sinceDate: %s is not in the required format of yyyy-MM-dd", sinceDate);
      throw new RuntimeException(msg);
    }
    // exclude if annotation was deprecated before cut off date of excludeSinceDate
    return deprecatedSince.before(excludeSinceDate);
  }

  private boolean shouldExcludeSinceVersion(String apiVersion) {
    apiVersion = apiVersion.trim();
    if (!Util.isYbVersionFormatValid(apiVersion)) {
      String errMsg = String.format("(Use 0.0.0.0) Wrong format sinceYBAVersion: %s", apiVersion);
      throw new RuntimeException(errMsg);
    }
    // exclude if api has been deprecated before excludeSinceVersion
    return Util.compareYbVersions(apiVersion, excludeSinceVersion) <= 0;
  }

  // For unit testing
  ExcludeKind getExcludeKind() {
    return excludeKind;
  }

  Date getExcludeSinceDate() {
    return excludeSinceDate;
  }

  String getExcludeSinceVersion() {
    return excludeSinceVersion;
  }
}
