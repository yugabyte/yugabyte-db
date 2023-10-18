/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.swagger;

import static junit.framework.TestCase.assertEquals;
import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertTrue;

import com.yugabyte.yw.common.swagger.YBADeprecationProcessor.ExcludeKind;
import com.yugabyte.yw.models.common.YBADeprecated;
import java.lang.annotation.Annotation;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class YBADeprecationProcessorTest {

  private String addMonthsToToday(int nMonths) {
    Calendar c1 = Calendar.getInstance();
    c1.add(Calendar.MONTH, nMonths);
    return new SimpleDateFormat("yyyy-MM-dd").format(c1.getTime());
  }

  private Object generateExcludeExpressions() {
    String twoYearsAgo = addMonthsToToday(-24);
    return new Object[] {
      new Object[] {"", true, ExcludeKind.NONE, null},
      new Object[] {"all", true, ExcludeKind.ALL, null},
      new Object[] {"24m", true, ExcludeKind.SINCE_DATE, twoYearsAgo},
      new Object[] {"2020-12-21", true, ExcludeKind.SINCE_DATE, "2020-12-21"},
      new Object[] {"2.15.3.0", true, ExcludeKind.SINCE_VERSION, null},
      new Object[] {"invalid expression", false, ExcludeKind.NONE, null},
    };
  }

  @Test
  @Parameters(method = "generateExcludeExpressions")
  public void testSetExcludeDeprecated(
      String excludeExpression, boolean isValid, ExcludeKind exKind, String exDate) {
    try {
      YBADeprecationProcessor ydp = new YBADeprecationProcessor(excludeExpression);
      assertTrue(isValid);
      String msg =
          String.format(
              "ExcludeKind for excludeExpression %s should be %s", excludeExpression, exKind);
      assertEquals(msg, ydp.getExcludeKind(), exKind);
      if (exKind == ExcludeKind.SINCE_DATE) {
        msg =
            String.format(
                "Exclude since date for excludeExpression %s should be %s",
                excludeExpression, exDate);
        String actualDate = new SimpleDateFormat("yyyy-MM-dd").format(ydp.getExcludeSinceDate());
        assertEquals(msg, actualDate, exDate);
      } else if (exKind == ExcludeKind.SINCE_VERSION) {
        msg =
            String.format(
                "Exclude since version for excludeExpression %s should be %s",
                excludeExpression, excludeExpression);
        assertEquals(msg, ydp.getExcludeSinceVersion(), excludeExpression);
      }
    } catch (RuntimeException e) {
      String msg =
          String.format("Should not throw exception for valid expression %s", excludeExpression);
      assertFalse(msg, isValid);
      String expectedMsg = String.format("--exclude-deprecated=%s is invalid", excludeExpression);
      msg = "Wrong exception thrown";
      assertTrue(msg, e.getMessage().equals(expectedMsg));
    }
  }

  private YBADeprecated newYBADeprecated(String sinceDate, String sinceVersion) {
    return new YBADeprecated() {
      @Override
      public String sinceDate() {
        return sinceDate;
      }

      @Override
      public String sinceYBAVersion() {
        return sinceVersion;
      }

      @Override
      public Class<? extends Annotation> annotationType() {
        return YBADeprecated.class;
      }

      @Override
      public String toString() {
        return String.format(
            "YBADeprecated(sinceDate: %s, sinceYBAVersion: %s)", sinceDate, sinceVersion);
      }
    };
  }

  private Object generateShouldExclude() {
    return new Object[] {
      // "" is ExcludeKind.NONE do not exclude any deprecated
      new Object[] {"", newYBADeprecated("2023-02-03", null), false},
      new Object[] {"", newYBADeprecated("3023-02-03", null), false},
      new Object[] {"", newYBADeprecated("1900-02-03", null), false},
      // "all" is ExcludeKind.ALL exclude all deprecated
      new Object[] {"all", newYBADeprecated("2023-02-28", null), true},
      new Object[] {"all", newYBADeprecated("3000-02-28", null), true},
      new Object[] {"all", newYBADeprecated("1900-02-28", null), true},
      // "24m" is ExcludeKind.SINCE_DATE exclude deprecated 24 months before now
      new Object[] {"24m", newYBADeprecated(addMonthsToToday(-30), null), true},
      new Object[] {"24m", newYBADeprecated(addMonthsToToday(-24), null), true},
      new Object[] {"24m", newYBADeprecated(addMonthsToToday(-23), null), false},
      new Object[] {"24m", newYBADeprecated(addMonthsToToday(0), null), false},
      new Object[] {"24m", newYBADeprecated(addMonthsToToday(1), null), false},
      // "2020-12-21" is ExcludeKind.SINCE_DATE excludes any deprecated on or before that date
      new Object[] {"2020-12-21", newYBADeprecated("2020-12-20", null), true},
      new Object[] {"2020-12-21", newYBADeprecated("2020-12-21", null), false},
      new Object[] {"2020-12-21", newYBADeprecated("2020-12-22", null), false},
      // "2.15.3.0" EcludeKind.SINCE_VERSION excludes any deprecated on or before that version
      new Object[] {"2.15.3.0-b0", newYBADeprecated(null, "2.14.0.0"), true},
      new Object[] {"2.15.3.0-b0", newYBADeprecated(null, "2.15.2.1"), true},
      new Object[] {"2.15.3.0", newYBADeprecated(null, "2.15.3.0"), true},
      new Object[] {"2.15.3.0", newYBADeprecated(null, "2.15.3.1"), false},
      new Object[] {"2.15.3.0", newYBADeprecated(null, "2.16.0.0"), false},
    };
  }

  @Test
  @Parameters(method = "generateShouldExclude")
  public void testShouldExcludeDeprecated(
      String excludeExpression, YBADeprecated annotation, boolean shouldBeExcluded) {
    YBADeprecationProcessor ydp = new YBADeprecationProcessor(excludeExpression);
    assertEquals(shouldBeExcluded, ydp.shouldExcludeDeprecated(annotation));
  }
}
