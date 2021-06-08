/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.text.DecimalFormat;
import java.util.*;
import java.util.function.Consumer;

@Entity
public class AlertDefinition extends Model {

  private static final String QUERY_THRESHOLD_PLACEHOLDER = "{{ query_threshold }}";
  private static final DecimalFormat THRESHOLD_FORMAT = new DecimalFormat("0.#");

  public enum TargetType {
    @EnumValue("Universe")
    Universe(
        KnownAlertLabels.UNIVERSE_UUID,
        "{{ $labels.definition_name }} for {{ $labels.universe_name }} is firing");

    // TODO will need to store threshold and duration in alert definition itself
    // to be able to show better alert message. Also, will be able to use current {{ value }}
    // once we move alert resolution to Prometheus.
    private final KnownAlertLabels targetUuidLabel;
    private final String defaultMessageTemplate;

    TargetType(KnownAlertLabels targetUuidLabel, String defaultMessageTemplate) {
      this.targetUuidLabel = targetUuidLabel;
      this.defaultMessageTemplate = defaultMessageTemplate;
    }

    public String getDefaultMessageTemplate() {
      return defaultMessageTemplate;
    }

    public KnownAlertLabels getTargetUuidLabel() {
      return targetUuidLabel;
    }
  }

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @Enumerated(EnumType.STRING)
  private TargetType targetType;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  private String name;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  private String query;

  @Constraints.Required
  @Column(nullable = false)
  private int queryDurationSec = 15;

  @Constraints.Required
  @Column(nullable = false)
  private double queryThreshold;

  @Constraints.Required private boolean isActive = true;

  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @JsonIgnore
  private boolean configWritten = false;

  @Version
  @Column(nullable = false)
  private int version;

  @OneToMany(mappedBy = "definition", cascade = CascadeType.ALL, orphanRemoval = true)
  private List<AlertDefinitionLabel> labels;

  private static final Finder<UUID, AlertDefinition> find =
      new Finder<UUID, AlertDefinition>(AlertDefinition.class) {};

  public static int delete(AlertDefinitionFilter filter) {
    return createQueryByFilter(filter).delete();
  }

  public static List<AlertDefinition> list(AlertDefinitionFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  public static List<UUID> listIds(AlertDefinitionFilter filter) {
    return createQueryByFilter(filter).findIds();
  }

  public static void process(AlertDefinitionFilter filter, Consumer<AlertDefinition> consumer) {
    createQueryByFilter(filter).findEach(consumer);
  }

  private static ExpressionList<AlertDefinition> createQueryByFilter(AlertDefinitionFilter filter) {
    ExpressionList<AlertDefinition> query = find.query().fetch("labels").where();
    if (filter.getUuid() != null) {
      query.eq("uuid", filter.getUuid());
    }
    if (filter.getCustomerUuid() != null) {
      query.eq("customer_uuid", filter.getCustomerUuid());
    }
    if (filter.getName() != null) {
      query.eq("name", filter.getName());
    }
    if (filter.getActive() != null) {
      query.eq("is_active", filter.getActive());
    }
    if (filter.getConfigWritten() != null) {
      query.eq("config_written", filter.getConfigWritten());
    }
    if (filter.getLabel() != null) {
      query
          .eq("labels.key.name", filter.getLabel().getName())
          .eq("labels.value", filter.getLabel().getValue());
    }
    return query;
  }

  public UUID getUuid() {
    return uuid;
  }

  public void generateUUID() {
    this.uuid = UUID.randomUUID();
    this.labels.forEach(label -> label.setDefinition(this));
  }

  public TargetType getTargetType() {
    return targetType;
  }

  public void setTargetType(TargetType targetType) {
    this.targetType = targetType;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public String getQuery() {
    return query;
  }

  public void setQuery(String query) {
    this.query = query;
  }

  public int getQueryDurationSec() {
    return queryDurationSec;
  }

  public void setQueryDurationSec(int queryDurationSec) {
    this.queryDurationSec = queryDurationSec;
  }

  public double getQueryThreshold() {
    return queryThreshold;
  }

  public void setQueryThreshold(double queryThreshold) {
    this.queryThreshold = queryThreshold;
  }

  public boolean isActive() {
    return isActive;
  }

  public void setActive(boolean active) {
    isActive = active;
  }

  public UUID getCustomerUUID() {
    return customerUUID;
  }

  public void setCustomerUUID(UUID customerUUID) {
    this.customerUUID = customerUUID;
  }

  public boolean isConfigWritten() {
    return configWritten;
  }

  public void setConfigWritten(boolean configWritten) {
    this.configWritten = configWritten;
  }

  public List<AlertDefinitionLabel> getLabels() {
    return labels;
  }

  public List<AlertDefinitionLabel> getEffectiveLabels() {
    List<AlertDefinitionLabel> effectiveLabels = new ArrayList<>();
    effectiveLabels.add(
        new AlertDefinitionLabel(this, KnownAlertLabels.DEFINITION_UUID, uuid.toString()));
    effectiveLabels.add(new AlertDefinitionLabel(this, KnownAlertLabels.DEFINITION_NAME, name));
    effectiveLabels.add(
        new AlertDefinitionLabel(this, KnownAlertLabels.CUSTOMER_UUID, customerUUID.toString()));
    effectiveLabels.addAll(labels);
    return effectiveLabels;
  }

  public UUID getUniverseUUID() {
    return Optional.ofNullable(getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
        .map(UUID::fromString)
        .orElseThrow(
            () -> new IllegalStateException("Definition " + uuid + " does not have universe UUID"));
  }

  public String getLabelValue(KnownAlertLabels knownLabel) {
    return getLabelValue(knownLabel.labelName());
  }

  public String getLabelValue(String name) {
    return getEffectiveLabels()
        .stream()
        .filter(label -> name.equals(label.getName()))
        .map(AlertDefinitionLabel::getValue)
        .findFirst()
        .orElse(null);
  }

  public void setLabels(List<AlertDefinitionLabel> labels) {
    if (this.labels == null) {
      this.labels = labels;
    } else {
      // Ebean ORM requires us to update existing loaded field rather than replace it completely.
      this.labels.clear();
      this.labels.addAll(labels);
    }
    this.labels.forEach(label -> label.setDefinition(this));
  }

  public String getMessageTemplate() {
    // Will allow to store custom message templates later in definition, if needed.
    return targetType.getDefaultMessageTemplate();
  }

  public UUID getTargetUUID() {
    return UUID.fromString(getLabelValue(targetType.getTargetUuidLabel()));
  }

  public String getQueryWithThreshold() {
    return query.replace(QUERY_THRESHOLD_PLACEHOLDER, THRESHOLD_FORMAT.format(queryThreshold));
  }

  public boolean configEquals(AlertDefinition other) {
    if (Objects.equals(getName(), other.getName())
        && Objects.equals(getQuery(), other.getQuery())
        && Objects.equals(getQueryDurationSec(), other.getQueryDurationSec())
        && Objects.equals(getQueryThreshold(), other.getQueryThreshold())
        && Objects.equals(isActive(), other.isActive())
        && Objects.equals(getEffectiveLabels(), other.getEffectiveLabels())) {
      return true;
    }
    return false;
  }

  @Override
  public String toString() {
    return "AlertDefinition{"
        + "uuid="
        + uuid
        + ", targetType="
        + targetType
        + ", name='"
        + name
        + '\''
        + ", query='"
        + query
        + '\''
        + ", queryDurationSec='"
        + queryDurationSec
        + '\''
        + ", queryThreshold='"
        + queryThreshold
        + '\''
        + ", isActive="
        + isActive
        + ", customerUUID="
        + customerUUID
        + ", configWritten="
        + configWritten
        + ", labels="
        + labels
        + ", version="
        + version
        + '}';
  }
}
