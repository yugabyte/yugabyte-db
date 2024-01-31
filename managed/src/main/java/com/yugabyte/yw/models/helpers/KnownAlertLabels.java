/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

public enum KnownAlertLabels {
  CONFIGURATION_UUID,
  CONFIGURATION_TYPE,
  DEFINITION_UUID,
  DEFINITION_NAME,
  UNIVERSE_UUID,
  UNIVERSE_NAME,
  CUSTOMER_UUID,
  SOURCE_UUID,
  SOURCE_NAME,
  SOURCE_TYPE,
  ALERT_STATE,
  SEVERITY,
  THRESHOLD,
  NODE_NAME,
  NODE_PREFIX,
  INSTANCE,
  EXPORT_TYPE,
  TASK_TYPE,
  RESULT,
  ALERTNAME,
  MESSAGE,
  MAINTENANCE_WINDOW_UUIDS,
  ALERT_EXPRESSION,
  YBA_VERSION,

  DB_VERSION;

  public String labelName() {
    return name().toLowerCase();
  }
}
