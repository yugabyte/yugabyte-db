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

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

/** Rewrites the alert rules of templates that read a runtime config key via their parameters. */
public abstract class AlertTemplateParameterListener implements RuntimeConfigChangeListener {

  private final String keyPath;

  private final Set<AlertTemplate> templates;

  private final AlertConfigurationService alertConfigurationService;

  private final AlertDefinitionService alertDefinitionService;

  protected AlertTemplateParameterListener(
      String keyPath,
      Set<AlertTemplate> templates,
      AlertConfigurationService alertConfigurationService,
      AlertDefinitionService alertDefinitionService) {
    this.keyPath = keyPath;
    this.templates = templates;
    this.alertConfigurationService = alertConfigurationService;
    this.alertDefinitionService = alertDefinitionService;
  }

  public String getKeyPath() {
    return keyPath;
  }

  @Override
  public void processGlobal() {
    List<AlertConfiguration> affectedConfigs =
        alertConfigurationService.list(
            AlertConfigurationFilter.builder().templates(templates).build());
    processConfigs(affectedConfigs);
  }

  @Override
  public void processCustomer(Customer customer) {
    List<AlertConfiguration> affectedConfigs =
        alertConfigurationService.list(
            AlertConfigurationFilter.builder()
                .templates(templates)
                .customerUuid(customer.getUuid())
                .build());
    processConfigs(affectedConfigs);
  }

  @Override
  public void processUniverse(Universe universe) {
    List<AlertConfiguration> affectedConfigs =
        alertConfigurationService.list(
            AlertConfigurationFilter.builder()
                .templates(templates)
                .customerUuid(Customer.get(universe.getCustomerId()).getUuid())
                .build());
    processConfigs(affectedConfigs);
  }

  private void processConfigs(List<AlertConfiguration> configs) {
    if (configs.isEmpty()) {
      return;
    }
    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder()
            .configurationUuids(
                configs.stream().map(AlertConfiguration::getUuid).collect(Collectors.toSet()))
            .build();

    List<AlertDefinition> definitions = alertDefinitionService.list(definitionFilter);
    if (definitions.isEmpty()) {
      return;
    }
    definitions.forEach(d -> d.setConfigWritten(false));
    alertDefinitionService.save(definitions);
  }
}
