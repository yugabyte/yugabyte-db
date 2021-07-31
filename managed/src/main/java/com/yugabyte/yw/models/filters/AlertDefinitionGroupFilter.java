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

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

@Value
@Builder
public class AlertDefinitionGroupFilter {
  Set<UUID> uuids;
  UUID customerUuid;
  String name;
  Boolean active;
  AlertDefinitionGroup.TargetType targetType;
  AlertDefinitionTemplate template;
  UUID targetUuid;
  UUID routeUuid;

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public AlertDefinitionGroupFilterBuilder toBuilder() {
    AlertDefinitionGroupFilterBuilder result = AlertDefinitionGroupFilter.builder();
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
    if (template != null) {
      result.template(template);
    }
    if (targetUuid != null) {
      result.targetUuid(targetUuid);
    }
    if (routeUuid != null) {
      result.routeUuid(routeUuid);
    }
    return result;
  }

  public static class AlertDefinitionGroupFilterBuilder {
    Set<UUID> uuids = new HashSet<>();

    public AlertDefinitionGroupFilterBuilder uuid(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public AlertDefinitionGroupFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public AlertDefinitionGroupFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder name(@NonNull String name) {
      this.name = name;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder active(@NonNull Boolean active) {
      this.active = active;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder targetType(
        @NonNull AlertDefinitionGroup.TargetType targetType) {
      this.targetType = targetType;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder template(@NonNull AlertDefinitionTemplate template) {
      this.template = template;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder targetUuid(@NonNull UUID targetUuid) {
      this.targetUuid = targetUuid;
      return this;
    }

    public AlertDefinitionGroupFilterBuilder routeUuid(@NonNull UUID routeUuid) {
      this.routeUuid = routeUuid;
      return this;
    }
  }
}
