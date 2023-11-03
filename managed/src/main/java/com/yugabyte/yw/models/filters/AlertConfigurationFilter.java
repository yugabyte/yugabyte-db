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

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationTarget;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

@Value
@Builder
public class AlertConfigurationFilter {
  Set<UUID> uuids;
  UUID customerUuid;
  String name;
  Boolean active;
  AlertConfiguration.TargetType targetType;
  AlertConfigurationTarget target;
  Set<AlertTemplate> templates;
  Severity severity;
  DestinationType destinationType;
  UUID destinationUuid;
  Boolean suspended;

  public List<String> getTemplatesStr() {
    if (templates == null) {
      return null;
    }
    return templates.stream().map(AlertTemplate::name).collect(Collectors.toList());
  }

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public AlertConfigurationFilterBuilder toBuilder() {
    AlertConfigurationFilterBuilder result = AlertConfigurationFilter.builder();
    if (uuids != null) {
      result.uuids(uuids);
    }
    if (customerUuid != null) {
      result.customerUuid(customerUuid);
    }
    if (name != null) {
      result.name(name);
    }
    if (active != null) {
      result.active(active);
    }
    if (targetType != null) {
      result.targetType(targetType);
    }
    if (target != null) {
      result.target(target);
    }
    if (templates != null) {
      result.templates(templates);
    }
    if (severity != null) {
      result.severity(severity);
    }
    if (destinationType != null) {
      result.destinationType(destinationType);
    }
    if (destinationUuid != null) {
      result.destinationUuid(destinationUuid);
    }
    if (suspended != null) {
      result.suspended(suspended);
    }
    return result;
  }

  public static class AlertConfigurationFilterBuilder {
    Set<UUID> uuids = new HashSet<>();
    Set<AlertTemplate> templates = new HashSet<>();

    public AlertConfigurationFilterBuilder uuid(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public AlertConfigurationFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public AlertConfigurationFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public AlertConfigurationFilterBuilder name(@NonNull String name) {
      this.name = name;
      return this;
    }

    public AlertConfigurationFilterBuilder active(@NonNull Boolean active) {
      this.active = active;
      return this;
    }

    public AlertConfigurationFilterBuilder targetType(
        @NonNull AlertConfiguration.TargetType targetType) {
      this.targetType = targetType;
      return this;
    }

    public AlertConfigurationFilterBuilder target(@NonNull AlertConfigurationTarget target) {
      this.target = target;
      return this;
    }

    public AlertConfigurationFilterBuilder template(@NonNull AlertTemplate template) {
      this.templates.add(template);
      return this;
    }

    public AlertConfigurationFilterBuilder templates(@NonNull Collection<AlertTemplate> templates) {
      this.templates.addAll(templates);
      return this;
    }

    public AlertConfigurationFilterBuilder severity(@NonNull Severity severity) {
      this.severity = severity;
      return this;
    }

    public AlertConfigurationFilterBuilder destinationType(
        @NonNull DestinationType destinationType) {
      this.destinationType = destinationType;
      return this;
    }

    public AlertConfigurationFilterBuilder destinationUuid(@NonNull UUID destinationUuid) {
      this.destinationUuid = destinationUuid;
      return this;
    }
  }

  public enum DestinationType {
    NO_DESTINATION,
    DEFAULT_DESTINATION,
    SELECTED_DESTINATION
  }
}
