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

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import java.time.Duration;

public class CustomerConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Duration> taskGcRetentionDuration =
      new ConfKeyInfo<>(
          "yb.taskGC.task_retention_duration",
          ScopeType.CUSTOMER,
          "Task Garbage Collection Retention Duration",
          "We garbage collect stale tasks after this duration",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> isAuthEnforced =
      new ConfKeyInfo<>(
          "yb.universe.auth.is_enforced",
          ScopeType.CUSTOMER,
          "Enforce Auth",
          "Enforces users to enter password for YSQL/YCQL during Universe creation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> taskDbQueryLimit =
      new ConfKeyInfo<>(
          "yb.customer_task_db_query_limit",
          ScopeType.CUSTOMER,
          "Max Number of Customer Tasks to fetch",
          "Knob that can be used when there are too many customer tasks"
              + " overwhelming the server",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // Todo shashank
  public static final ConfKeyInfo<Duration> proxyEndpointTimeout =
      new ConfKeyInfo<>(
          "yb.proxy_endpoint_timeout",
          ScopeType.CUSTOMER,
          "Proxy Endpoint Timeout",
          "todo",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.BETA));

  public static final ConfKeyInfo<Duration> perfRecommendationRetentionDuration =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cleanup.rec_retention_duration",
          ScopeType.CUSTOMER,
          "Perf Recommendation Collection Retention Duration",
          "Conf key that represents the duration of time the perf-advisor recommendation is valid. "
              + "Once this duration is exceeded, the recommendation entry is marked"
              + " as stale and deleted.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> showUICost =
      new ConfKeyInfo<>(
          "yb.ui.show_cost",
          ScopeType.CUSTOMER,
          "Show costs in UI",
          "Option to enable/disable costs in UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
}
