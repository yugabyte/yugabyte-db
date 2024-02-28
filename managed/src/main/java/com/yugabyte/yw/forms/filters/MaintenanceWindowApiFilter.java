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

import com.yugabyte.yw.models.MaintenanceWindow.State;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;

@Data
@NoArgsConstructor
public class MaintenanceWindowApiFilter {
  Set<UUID> uuids;
  Set<State> states;

  public MaintenanceWindowFilter toFilter() {
    MaintenanceWindowFilter.MaintenanceWindowFilterBuilder builder =
        MaintenanceWindowFilter.builder();
    if (!CollectionUtils.isEmpty(uuids)) {
      builder.uuids(uuids);
    }
    if (!CollectionUtils.isEmpty(states)) {
      builder.states(states);
    }
    return builder.build();
  }
}
