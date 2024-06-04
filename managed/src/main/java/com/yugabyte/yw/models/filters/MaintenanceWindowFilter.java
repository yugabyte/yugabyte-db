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

import com.yugabyte.yw.models.MaintenanceWindow.State;
import java.util.Collection;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

@Value
@Builder
public class MaintenanceWindowFilter {
  UUID customerUuid;
  Set<UUID> uuids;
  Set<State> states;
  Date endTimeBefore;

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public MaintenanceWindowFilterBuilder toBuilder() {
    MaintenanceWindowFilterBuilder result = MaintenanceWindowFilter.builder();
    if (uuids != null) {
      result.uuids(uuids);
    }
    if (customerUuid != null) {
      result.customerUuid(customerUuid);
    }
    if (states != null) {
      result.states(states);
    }
    if (endTimeBefore != null) {
      result.endTimeBefore(endTimeBefore);
    }
    return result;
  }

  public static class MaintenanceWindowFilterBuilder {
    Set<UUID> uuids = new HashSet<>();
    Set<State> states = new HashSet<>();

    public MaintenanceWindowFilterBuilder uuid(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public MaintenanceWindowFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public MaintenanceWindowFilterBuilder state(@NonNull State state) {
      this.states.add(state);
      return this;
    }

    public MaintenanceWindowFilterBuilder states(@NonNull Collection<State> states) {
      this.states.addAll(states);
      return this;
    }
  }
}
