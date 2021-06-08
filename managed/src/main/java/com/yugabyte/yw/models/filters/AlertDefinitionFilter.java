/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.AlertDefinitionLabel;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;

import java.util.UUID;

public class AlertDefinitionFilter {
  private UUID uuid;
  private UUID customerUuid;
  private String name;
  private AlertDefinitionLabel label;
  private Boolean isActive;
  private Boolean isConfigWritten;

  public UUID getUuid() {
    return uuid;
  }

  public AlertDefinitionFilter setUuid(UUID uuid) {
    this.uuid = uuid;
    return this;
  }

  public UUID getCustomerUuid() {
    return customerUuid;
  }

  public AlertDefinitionFilter setCustomerUuid(UUID customerUuid) {
    this.customerUuid = customerUuid;
    return this;
  }

  public String getName() {
    return name;
  }

  public AlertDefinitionFilter setName(String name) {
    this.name = name;
    return this;
  }

  public AlertDefinitionLabel getLabel() {
    return label;
  }

  public AlertDefinitionFilter setLabel(AlertDefinitionLabel label) {
    this.label = label;
    return this;
  }

  public AlertDefinitionFilter setLabel(String name, String value) {
    this.label = new AlertDefinitionLabel(name, value);
    return this;
  }

  public AlertDefinitionFilter setLabel(KnownAlertLabels name, String value) {
    this.label = new AlertDefinitionLabel(name, value);
    return this;
  }

  public Boolean getActive() {
    return isActive;
  }

  public AlertDefinitionFilter setActive(Boolean active) {
    isActive = active;
    return this;
  }

  public Boolean getConfigWritten() {
    return isConfigWritten;
  }

  public AlertDefinitionFilter setConfigWritten(Boolean configWritten) {
    isConfigWritten = configWritten;
    return this;
  }
}
