/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.UniqueKeyListValue;
import io.ebean.Model;
import java.util.Objects;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.ManyToOne;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Data
@Entity
@EqualsAndHashCode(callSuper = false)
@ToString
public class MetricLabel extends Model implements UniqueKeyListValue<MetricLabel> {

  @EmbeddedId private MetricLabelKey key;

  @Column(nullable = false)
  private String value;

  @ManyToOne @JsonIgnore @EqualsAndHashCode.Exclude @ToString.Exclude private Metric metric;

  @Column(nullable = false)
  private boolean sourceLabel;

  public MetricLabel() {
    this.key = new MetricLabelKey();
  }

  public MetricLabel(String name, String value) {
    this();
    key.setName(name);
    this.value = value;
  }

  public MetricLabel(KnownAlertLabels label, String value) {
    this(label.labelName(), value);
  }

  public MetricLabel(Metric metric, String name, String value) {
    this(name, value);
    setMetric(metric);
  }

  public String getName() {
    return key.getName();
  }

  public void setMetric(Metric metric) {
    this.metric = metric;
    key.setMetricUuid(metric.getUuid());
  }

  @Override
  @JsonIgnore
  public boolean keyEquals(MetricLabel other) {
    return Objects.equals(getName(), other.getName());
  }

  @Override
  @JsonIgnore
  public boolean valueEquals(MetricLabel other) {
    return keyEquals(other) && Objects.equals(getValue(), other.getValue());
  }
}
