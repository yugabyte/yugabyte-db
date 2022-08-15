/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.helpers;

import io.ebean.annotation.EnumValue;

public enum TimeUnit {
  @EnumValue("NanoSeconds")
  NANOSECONDS,

  @EnumValue("MicroSeconds")
  MICROSECONDS,

  @EnumValue("MilliSeconds")
  MILLISECONDS,

  @EnumValue("Seconds")
  SECONDS,

  @EnumValue("Minutes")
  MINUTES,

  @EnumValue("Hours")
  HOURS,

  @EnumValue("Days")
  DAYS,

  @EnumValue("Months")
  MONTHS,

  @EnumValue("Years")
  YEARS
}
