// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;

import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.prometheus.client.Collector;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
@Slf4j
public class Metric {

  // For now only support gauge.
  public enum Type {
    GAUGE(Collector.Type.GAUGE);

    private final Collector.Type prometheusType;

    Type(Collector.Type prometheusType) {
      this.prometheusType = prometheusType;
    }

    public Collector.Type getPrometheusType() {
      return prometheusType;
    }
  }

  private UUID customerUUID;

  private String name;

  private String help;

  private String unit;

  private Type type;

  private Date createTime = nowWithoutMillis();

  private Date updateTime = nowWithoutMillis();

  private Date expireTime;

  private UUID sourceUuid;

  private Set<String> keyLabels = new HashSet<>();

  private Map<String, String> labels = new TreeMap<>();

  private Double value;

  private boolean deleted;

  public String getLabelValue(KnownAlertLabels knownLabel) {
    return getLabelValue(knownLabel.labelName());
  }

  public String getLabelValue(String name) {
    return labels.get(name);
  }

  public Metric setKeyLabel(KnownAlertLabels label, String value) {
    setLabel(label.labelName(), value);
    keyLabels.add(label.labelName());
    return this;
  }

  public Metric setKeyLabel(String label, String value) {
    setLabel(label, value);
    keyLabels.add(label);
    return this;
  }

  public Metric setLabel(KnownAlertLabels label, String value) {
    return setLabel(label.labelName(), value);
  }

  public Metric setLabel(String name, String value) {
    labels.put(name, value);
    keyLabels.remove(name);
    return this;
  }

  public Metric setLabels(Map<String, String> labels) {
    this.labels = new HashMap<>(labels);
    this.keyLabels.clear();
    return this;
  }

  public Map<String, String> getKeyLabelValues() {
    return labels.entrySet().stream()
        .filter(e -> keyLabels.contains(e.getKey()))
        .collect(Collectors.toMap(Entry::getKey, Entry::getValue));
  }
}
