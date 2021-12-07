/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import com.yugabyte.yw.models.AlertDefinitionLabel;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MetricLabel;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

public class MetricLabelsBuilder {
  public static String[] UNIVERSE_LABELS = {
    KnownAlertLabels.UNIVERSE_UUID.labelName(),
    KnownAlertLabels.UNIVERSE_NAME.labelName(),
    KnownAlertLabels.SOURCE_UUID.labelName(),
    KnownAlertLabels.SOURCE_NAME.labelName(),
    KnownAlertLabels.SOURCE_TYPE.labelName()
  };

  private final List<MetricLabel> labels = new ArrayList<>();

  public static MetricLabelsBuilder create() {
    return new MetricLabelsBuilder();
  }

  public MetricLabelsBuilder appendUniverse(Universe universe) {
    labels.add(new MetricLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString()));
    labels.add(new MetricLabel(KnownAlertLabels.UNIVERSE_NAME, universe.name));
    return this;
  }

  public MetricLabelsBuilder appendSource(Universe universe) {
    appendUniverse(universe);
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_UUID, universe.universeUUID.toString()));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_NAME, universe.name));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_TYPE, "universe"));
    return this;
  }

  public MetricLabelsBuilder appendSource(Customer customer) {
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_UUID, customer.getUuid().toString()));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_NAME, customer.name));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_TYPE, "customer"));
    return this;
  }

  public MetricLabelsBuilder appendSource(AlertChannel channel) {
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_UUID, channel.getUuid().toString()));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_NAME, channel.getName()));
    labels.add(new MetricLabel(KnownAlertLabels.SOURCE_TYPE, "alert channel"));
    return this;
  }

  public List<AlertDefinitionLabel> getDefinitionLabels() {
    return labels
        .stream()
        .map(label -> new AlertDefinitionLabel(label.getName(), label.getValue()))
        .collect(Collectors.toList());
  }

  public List<AlertLabel> getAlertLabels() {
    return labels
        .stream()
        .map(label -> new AlertLabel(label.getName(), label.getValue()))
        .collect(Collectors.toList());
  }

  public List<MetricLabel> getMetricLabels() {
    return labels;
  }

  public String[] getPrometheusValues() {
    return labels.stream().map(MetricLabel::getValue).toArray(String[]::new);
  }
}
