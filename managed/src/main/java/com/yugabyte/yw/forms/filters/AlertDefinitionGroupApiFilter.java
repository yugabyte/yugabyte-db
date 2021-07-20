/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections.CollectionUtils;

@Data
@NoArgsConstructor
public class AlertDefinitionGroupApiFilter {
  private Set<UUID> uuids;
  private String name;
  private Boolean active;
  private AlertDefinitionGroup.TargetType targetType;
  private AlertDefinitionTemplate template;
  private UUID routeUuid;

  public AlertDefinitionGroupFilter toFilter() {
    AlertDefinitionGroupFilter.AlertDefinitionGroupFilterBuilder builder =
        AlertDefinitionGroupFilter.builder();
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
    if (template != null) {
      builder.template(template);
    }
    if (routeUuid != null) {
      builder.routeUuid(routeUuid);
    }
    return builder.build();
  }
}
