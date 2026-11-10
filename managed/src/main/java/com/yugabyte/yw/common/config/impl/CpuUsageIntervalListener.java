/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class CpuUsageIntervalListener extends AlertTemplateParameterListener {

  @Inject
  public CpuUsageIntervalListener(
      AlertConfigurationService alertConfigurationService,
      AlertDefinitionService alertDefinitionService) {
    super(
        UniverseConfKeys.cpuUsageAggregationInterval.getKey(),
        ImmutableSet.of(AlertTemplate.NODE_CPU_USAGE, AlertTemplate.DB_K8S_CPU_USAGE),
        alertConfigurationService,
        alertDefinitionService);
  }
}
