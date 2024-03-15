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

import static com.yugabyte.yw.common.Util.doubleToString;
import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static com.yugabyte.yw.models.helpers.CommonUtils.setUniqueListValue;
import static com.yugabyte.yw.models.helpers.CommonUtils.setUniqueListValues;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.PersistenceContextScope;
import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Version;
import java.util.Collection;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@Entity
@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false)
public class AlertDefinition extends Model {

  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @NotNull
  @Column(nullable = false)
  private UUID customerUUID;

  @NotNull
  @Column(nullable = false)
  private UUID configurationUUID;

  @NotNull
  @Column(nullable = false)
  @JsonIgnore
  private boolean configWritten = false;

  @NotNull
  @Column(nullable = false)
  @JsonIgnore
  private boolean active = true;

  @Version
  @Column(nullable = false)
  private int version;

  @OneToMany(mappedBy = "definition", cascade = CascadeType.ALL, orphanRemoval = true)
  @Valid
  private List<AlertDefinitionLabel> labels;

  private static final Finder<UUID, AlertDefinition> find =
      new Finder<UUID, AlertDefinition>(AlertDefinition.class) {};

  public static ExpressionList<AlertDefinition> createQueryByFilter(AlertDefinitionFilter filter) {
    ExpressionList<AlertDefinition> query =
        find.query()
            .setPersistenceContextScope(PersistenceContextScope.QUERY)
            .fetch("labels")
            .where();
    appendInClause(query, "uuid", filter.getUuids());
    if (filter.getCustomerUuid() != null) {
      query.eq("customerUUID", filter.getCustomerUuid());
    }
    appendInClause(query, "configurationUUID", filter.getConfigurationUuids());
    if (filter.getConfigWritten() != null) {
      query.eq("configWritten", filter.getConfigWritten());
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

  @JsonIgnore
  public boolean isNew() {
    return uuid == null;
  }

  public Collection<AlertDefinitionLabel> getEffectiveLabels(
      AlertTemplateDescription templateDescription,
      AlertConfiguration configuration,
      AlertTemplateSettings templateSettings,
      AlertConfiguration.Severity severity) {
    Map<String, AlertDefinitionLabel> effectiveLabels = new LinkedHashMap<>();
    if (templateSettings != null) {
      templateSettings
          .getLabels()
          .forEach((label, value) -> putLabel(effectiveLabels, label, value));
    }
    if (configuration.getLabels() != null) {
      configuration.getLabels().forEach((label, value) -> putLabel(effectiveLabels, label, value));
    }
    // Don't allow to override default labels with template settings
    putLabel(
        effectiveLabels, KnownAlertLabels.CONFIGURATION_UUID, configuration.getUuid().toString());
    putLabel(
        effectiveLabels, KnownAlertLabels.CONFIGURATION_TYPE, configuration.getTargetType().name());
    putLabel(effectiveLabels, KnownAlertLabels.ALERT_TYPE, configuration.getTemplate().name());
    putLabel(effectiveLabels, KnownAlertLabels.DEFINITION_UUID, uuid.toString());
    putLabel(effectiveLabels, KnownAlertLabels.DEFINITION_NAME, configuration.getName());
    putLabel(effectiveLabels, KnownAlertLabels.CUSTOMER_UUID, customerUUID.toString());
    putLabel(effectiveLabels, KnownAlertLabels.SEVERITY, severity.name());
    putLabel(
        effectiveLabels,
        KnownAlertLabels.THRESHOLD,
        doubleToString(configuration.getThresholds().get(severity).getThreshold()));
    putLabel(
        effectiveLabels,
        KnownAlertLabels.ALERT_EXPRESSION,
        templateDescription.getQueryWithThreshold(
            this, configuration.getThresholds().get(severity)));

    labels.forEach(label -> putLabel(effectiveLabels, label.getName(), label.getValue()));
    return effectiveLabels.values();
  }

  private void putLabel(
      Map<String, AlertDefinitionLabel> labelMap, KnownAlertLabels label, String value) {
    putLabel(labelMap, label.labelName(), value);
  }

  private void putLabel(Map<String, AlertDefinitionLabel> labelMap, String label, String value) {
    labelMap.put(label, new AlertDefinitionLabel(this, label, value));
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
    return getLabels().stream()
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

  public AlertDefinition removeLabel(KnownAlertLabels labelName) {
    AlertDefinitionLabel toRemove =
        labels.stream()
            .filter(label -> label.getName().equals(labelName.labelName()))
            .findFirst()
            .orElse(null);
    if (toRemove == null) {
      return this;
    }

    this.labels.remove(toRemove);
    return this;
  }

  public List<AlertDefinitionLabel> getLabels() {
    return labels.stream()
        .sorted(Comparator.comparing(AlertDefinitionLabel::getName))
        .collect(Collectors.toList());
  }
}
