/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter.AlertConfigurationFilterBuilder;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter.DestinationType;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;

@Data
@NoArgsConstructor
public class AlertConfigurationApiFilter {

  @ApiModelProperty(value = "The uuids of the alert configurations.")
  private Set<UUID> uuids;

  @ApiModelProperty(value = "The name of the alert configuration.")
  private String name;

  @ApiModelProperty(value = "Whether the alert configuration is active.")
  private Boolean active;

  @ApiModelProperty(value = "The target type of the alert configuration.")
  private AlertConfiguration.TargetType targetType;

  @ApiModelProperty(value = "The target of the alert configuration.")
  private AlertConfigurationTarget target;

  @ApiModelProperty(value = "The template of the alert configuration.")
  private AlertTemplate template;

  @ApiModelProperty(value = "The severity of the alert configuration.")
  private Severity severity;

  @ApiModelProperty(value = "The destination type of the alert configuration. ")
  private DestinationType destinationType;

  @ApiModelProperty(value = "The destination uuid of the alert configuration. ")
  private UUID destinationUuid;

  public AlertConfigurationFilter toFilter() {
    AlertConfigurationFilterBuilder builder = AlertConfigurationFilter.builder();
    if (!CollectionUtils.isEmpty(uuids)) {
      builder.uuids(uuids);
    }
    if (name != null) {
      builder.name(name);
    }
    if (active != null) {
      builder.active(active);
    }
    if (targetType != null) {
      builder.targetType(targetType);
    }
    if (target != null) {
      builder.target(target);
    }
    if (template != null) {
      builder.template(template);
    }
    if (severity != null) {
      builder.severity(severity);
    }
    if (destinationType != null) {
      builder.destinationType(destinationType);
    }
    if (destinationUuid != null) {
      builder.destinationUuid(destinationUuid);
    }
    return builder.build();
  }
}
