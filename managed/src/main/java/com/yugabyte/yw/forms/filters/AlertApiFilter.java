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

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.filters.AlertFilter;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Data
@NoArgsConstructor
public class AlertApiFilter {
  private Set<UUID> uuids;
  private UUID configurationUuid;
  private Set<AlertConfiguration.Severity> severities;
  private Set<AlertConfiguration.TargetType> configurationTypes;
  private Set<Alert.State> states;
  private String sourceName;
  private Set<UUID> sourceUUIDs;

  public AlertFilter toFilter() {
    AlertFilter.AlertFilterBuilder builder = AlertFilter.builder();
    if (!CollectionUtils.isEmpty(uuids)) {
      builder.uuids(uuids);
    }
    if (configurationUuid != null) {
      builder.configurationUuid(configurationUuid);
    }
    if (!CollectionUtils.isEmpty(severities)) {
      builder.severities(severities);
    }
    if (!CollectionUtils.isEmpty(configurationTypes)) {
      builder.configurationTypes(configurationTypes);
    }
    if (!CollectionUtils.isEmpty(states)) {
      builder.states(states);
    }
    if (!StringUtils.isEmpty(sourceName)) {
      builder.sourceName(sourceName);
    }
    if (!CollectionUtils.isEmpty(sourceUUIDs)) {
      builder.sourceUUIDs(sourceUUIDs);
    }
    return builder.build();
  }
}
