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

import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertDefinitionLabel;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

public class MetricLabelsBuilder {
  public static String[] UNIVERSE_LABELS = {
    KnownAlertLabels.UNIVERSE_UUID.labelName(),
    KnownAlertLabels.UNIVERSE_NAME.labelName(),
    KnownAlertLabels.NODE_PREFIX.labelName(),
    KnownAlertLabels.SOURCE_UUID.labelName(),
    KnownAlertLabels.SOURCE_NAME.labelName(),
    KnownAlertLabels.SOURCE_TYPE.labelName()
  };

  public static String[] CUSTOMER_LABELS = {
    KnownAlertLabels.CUSTOMER_UUID.labelName(),
    KnownAlertLabels.CUSTOMER_NAME.labelName(),
    KnownAlertLabels.SOURCE_UUID.labelName(),
    KnownAlertLabels.SOURCE_NAME.labelName(),
    KnownAlertLabels.SOURCE_TYPE.labelName()
  };

  private final Map<String, String> labels = new HashMap<>();

  public static MetricLabelsBuilder create() {
    return new MetricLabelsBuilder();
  }

  public MetricLabelsBuilder appendUniverse(Universe universe) {
    labels.put(KnownAlertLabels.UNIVERSE_UUID.labelName(), universe.getUniverseUUID().toString());
    labels.put(KnownAlertLabels.UNIVERSE_NAME.labelName(), universe.getName());
    labels.put(KnownAlertLabels.NODE_PREFIX.labelName(), universe.getUniverseDetails().nodePrefix);
    return this;
  }

  public MetricLabelsBuilder appendSource(Universe universe) {
    appendUniverse(universe);
    labels.put(KnownAlertLabels.SOURCE_UUID.labelName(), universe.getUniverseUUID().toString());
    labels.put(KnownAlertLabels.SOURCE_NAME.labelName(), universe.getName());
    labels.put(KnownAlertLabels.SOURCE_TYPE.labelName(), "universe");
    return this;
  }

  public MetricLabelsBuilder appendCustomer(Customer customer) {
    labels.put(KnownAlertLabels.CUSTOMER_CODE.labelName(), customer.getCode());
    labels.put(KnownAlertLabels.CUSTOMER_NAME.labelName(), customer.getName());
    return this;
  }

  public MetricLabelsBuilder appendSource(Customer customer) {
    labels.put(KnownAlertLabels.SOURCE_UUID.labelName(), customer.getUuid().toString());
    labels.put(KnownAlertLabels.SOURCE_NAME.labelName(), customer.getName());
    labels.put(KnownAlertLabels.SOURCE_TYPE.labelName(), "customer");
    return this;
  }

  public MetricLabelsBuilder appendSource(AlertChannel channel) {
    labels.put(KnownAlertLabels.SOURCE_UUID.labelName(), channel.getUuid().toString());
    labels.put(KnownAlertLabels.SOURCE_NAME.labelName(), channel.getName());
    labels.put(KnownAlertLabels.SOURCE_TYPE.labelName(), "alert channel");
    return this;
  }

  public List<AlertDefinitionLabel> getDefinitionLabels() {
    return labels.entrySet().stream()
        .map(label -> new AlertDefinitionLabel(label.getKey(), label.getValue()))
        .collect(Collectors.toList());
  }

  public List<AlertLabel> getAlertLabels() {
    return labels.entrySet().stream()
        .map(label -> new AlertLabel(label.getKey(), label.getValue()))
        .collect(Collectors.toList());
  }

  public Map<String, String> getMetricLabels() {
    return labels;
  }

  public String[] getPrometheusValues() {
    return labels.values().toArray(new String[0]);
  }
}
