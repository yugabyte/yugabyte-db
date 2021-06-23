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
import com.yugabyte.yw.common.alerts.AlertLabelsProvider;
import com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.text.DecimalFormat;
import java.util.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.models.helpers.CommonUtils.*;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
public class AlertDefinition extends Model implements AlertLabelsProvider {

  private static final String QUERY_THRESHOLD_PLACEHOLDER = "{{ query_threshold }}";
  private static final DecimalFormat THRESHOLD_FORMAT = new DecimalFormat("0.#");

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

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

  @Constraints.Required private boolean active = true;

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

  public static ExpressionList<AlertDefinition> createQueryByFilter(AlertDefinitionFilter filter) {
    ExpressionList<AlertDefinition> query = find.query().fetch("labels").where();
    appendInClause(query, "uuid", filter.getUuids());
    if (filter.getCustomerUuid() != null) {
      query.eq("customer_uuid", filter.getCustomerUuid());
    }
    if (filter.getName() != null) {
      query.eq("name", filter.getName());
    }
    if (filter.getActive() != null) {
      query.eq("active", filter.getActive());
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

  public AlertDefinition generateUUID() {
    this.uuid = UUID.randomUUID();
    this.labels.forEach(label -> label.setDefinition(this));
    return this;
  }

  public List<AlertDefinitionLabel> getEffectiveLabels() {
    List<AlertDefinitionLabel> effectiveLabels = new ArrayList<>();
    effectiveLabels.add(
        new AlertDefinitionLabel(this, KnownAlertLabels.DEFINITION_UUID, uuid.toString()));
    effectiveLabels.add(new AlertDefinitionLabel(this, KnownAlertLabels.DEFINITION_NAME, name));
    effectiveLabels.add(
        new AlertDefinitionLabel(
            this, KnownAlertLabels.DEFINITION_ACTIVE, String.valueOf(isActive())));
    effectiveLabels.add(
        new AlertDefinitionLabel(this, KnownAlertLabels.CUSTOMER_UUID, customerUUID.toString()));
    effectiveLabels.add(
        new AlertDefinitionLabel(
            this, KnownAlertLabels.ERROR_CODE, KnownAlertCodes.CUSTOMER_ALERT.name()));
    effectiveLabels.add(
        new AlertDefinitionLabel(this, KnownAlertLabels.ALERT_TYPE, KnownAlertTypes.Error.name()));
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

  public AlertDefinition setLabel(KnownAlertLabels label, String value) {
    return setLabel(label.labelName(), value);
  }

  public AlertDefinition setLabel(String name, String value) {
    AlertDefinitionLabel toAdd = new AlertDefinitionLabel(this, name, value);
    this.labels = setUniqueListValue(labels, toAdd);
    return this;
  }

  public AlertDefinition setLabels(List<AlertDefinitionLabel> labels) {
    this.labels = setUniqueListValues(this.labels, labels);
    this.labels.forEach(label -> label.setDefinition(this));
    return this;
  }

  public List<AlertDefinitionLabel> getLabels() {
    return labels
        .stream()
        .sorted(Comparator.comparing(AlertDefinitionLabel::getName))
        .collect(Collectors.toList());
  }

  public String getMessageTemplate() {
    // Will allow to store custom message templates later in definition, if needed.
    // Note that we're replacing definition labels right here as Prometheus is not doing it:
    // https://groups.google.com/g/prometheus-users/c/oUJOv7_9k8U/m/9ytCiLmmAQAJ
    AlertTemplateSubstitutor<AlertDefinition> substitutor = new AlertTemplateSubstitutor<>(this);
    return substitutor.replace(
        "{{ $labels.definition_name }} for {{ $labels.target_name }} is firing");
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
}
