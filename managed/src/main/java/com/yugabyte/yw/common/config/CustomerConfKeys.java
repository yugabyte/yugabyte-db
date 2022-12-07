/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import java.time.Duration;

// TODO: Similar config key lists for other scopes
public class CustomerConfKeys extends RuntimeConfigKeysModule {

  static ConfKeyInfo<Duration> taskGcRetentionDuration =
      new ConfKeyInfo<>(
          "yb.taskGC.task_retention_duration",
          ScopeType.CUSTOMER,
          "Task Garbage Collection Retention Duration",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.DurationType);

  // TODO: Add other keys
}
