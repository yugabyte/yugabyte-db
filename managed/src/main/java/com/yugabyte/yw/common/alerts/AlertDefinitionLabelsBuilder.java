/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

public class AlertDefinitionLabelsBuilder {
  private final List<AlertDefinitionLabel> labels = new ArrayList<>();

  public static AlertDefinitionLabelsBuilder create() {
    return new AlertDefinitionLabelsBuilder();
  }

  public AlertDefinitionLabelsBuilder appendUniverse(Universe universe) {
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString()));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.UNIVERSE_NAME, universe.name));
    return this;
  }

  public AlertDefinitionLabelsBuilder appendTarget(Universe universe) {
    appendUniverse(universe);
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.TARGET_UUID, universe.universeUUID.toString()));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.TARGET_NAME, universe.name));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.TARGET_TYPE, "universe"));
    return this;
  }

  public AlertDefinitionLabelsBuilder appendTarget(Customer customer) {
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.TARGET_UUID, customer.getUuid().toString()));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.TARGET_NAME, customer.name));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.TARGET_TYPE, "customer"));
    return this;
  }

  public AlertDefinitionLabelsBuilder appendTarget(AlertReceiver receiver) {
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.TARGET_UUID, receiver.getUuid().toString()));
    // TODO - may want to have receiver name - even to display a list of receivers.
    // Also, for some reason, we're not storing receiver type as receiver field - need to fix that
    // as well.
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.TARGET_NAME, receiver.getUuid().toString()));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.TARGET_TYPE, "alert receiver"));
    return this;
  }

  public List<AlertDefinitionLabel> get() {
    return labels;
  }

  public List<AlertLabel> getAlertLabels() {
    return labels
        .stream()
        .map(label -> new AlertLabel(label.getName(), label.getValue()))
        .collect(Collectors.toList());
  }
}
