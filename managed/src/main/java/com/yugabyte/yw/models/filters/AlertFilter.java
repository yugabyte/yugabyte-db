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

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

import java.util.*;

@Value
@Builder
public class AlertFilter {
  Set<UUID> uuids;
  Set<UUID> excludeUuids;
  UUID customerUuid;
  String errorCode;
  Set<Alert.State> states;
  Set<Alert.State> targetStates;
  Set<UUID> definitionUuids;
  AlertLabel label;

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public AlertFilterBuilder toBuilder() {
    AlertFilterBuilder result = AlertFilter.builder();
    if (uuids != null) {
      result.uuids(uuids);
    }
    if (excludeUuids != null) {
      result.excludeUuids(excludeUuids);
    }
    if (customerUuid != null) {
      result.customerUuid(customerUuid);
    }
    if (errorCode != null) {
      result.errorCode(errorCode);
    }
    if (label != null) {
      result.label(label);
    }
    if (states != null) {
      result.states(states);
    }
    if (targetStates != null) {
      result.targetStates(targetStates);
    }
    if (definitionUuids != null) {
      result.definitionUuids(definitionUuids);
    }
    return result;
  }

  public static class AlertFilterBuilder {
    Set<UUID> uuids = new HashSet<>();
    Set<UUID> excludeUuids = new HashSet<>();
    UUID customerUuid;
    String errorCode;
    Set<Alert.State> states = EnumSet.noneOf(Alert.State.class);
    Set<Alert.State> targetStates = EnumSet.noneOf(Alert.State.class);
    AlertLabel label;
    Set<UUID> definitionUuids = new HashSet<>();

    public AlertFilterBuilder uuids(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public AlertFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public AlertFilterBuilder excludeUuids(@NonNull UUID uuid) {
      this.excludeUuids.add(uuid);
      return this;
    }

    public AlertFilterBuilder excludeUuids(@NonNull Collection<UUID> uuids) {
      this.excludeUuids.addAll(uuids);
      return this;
    }

    public AlertFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public AlertFilterBuilder states(@NonNull Alert.State... state) {
      states.addAll(Arrays.asList(state));
      return this;
    }

    public AlertFilterBuilder states(@NonNull Set<Alert.State> states) {
      this.states.addAll(states);
      return this;
    }

    public AlertFilterBuilder targetStates(@NonNull Alert.State... state) {
      targetStates.addAll(Arrays.asList(state));
      return this;
    }

    public AlertFilterBuilder targetStates(@NonNull Set<Alert.State> states) {
      this.targetStates.addAll(states);
      return this;
    }

    public AlertFilterBuilder label(@NonNull KnownAlertLabels name, @NonNull String value) {
      label = new AlertLabel(name.labelName(), value);
      return this;
    }

    public AlertFilterBuilder label(@NonNull String name, @NonNull String value) {
      label = new AlertLabel(name, value);
      return this;
    }

    public AlertFilterBuilder label(@NonNull AlertLabel label) {
      this.label = label;
      return this;
    }

    public AlertFilterBuilder errorCode(@NonNull String errorCode) {
      this.errorCode = errorCode;
      return this;
    }

    public AlertFilterBuilder errorCode(@NonNull KnownAlertCodes errorCode) {
      this.errorCode = errorCode.name();
      return this;
    }

    public AlertFilterBuilder definitionUuids(@NonNull UUID uuid) {
      this.definitionUuids.add(uuid);
      return this;
    }

    public AlertFilterBuilder definitionUuids(Collection<UUID> definitionUuids) {
      this.definitionUuids = new HashSet<>(definitionUuids);
      return this;
    }
  }
}
