// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.config.impl;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class NodeMaintenanceServiceMaxAgeListener extends AlertTemplateParameterListener {

  @Inject
  public NodeMaintenanceServiceMaxAgeListener(
      AlertConfigurationService alertConfigurationService,
      AlertDefinitionService alertDefinitionService) {
    super(
        UniverseConfKeys.nodeMaintenanceServiceMaxAge.getKey(),
        ImmutableSet.of(AlertTemplate.NODE_MAINTENANCE_SERVICE_NOT_RUN),
        alertConfigurationService,
        alertDefinitionService);
  }
}
