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

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import play.data.validation.Constraints;

import javax.persistence.CascadeType;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.OneToMany;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;

import static play.mvc.Http.Status.BAD_REQUEST;

@Entity
public class AlertDefinition extends Model {

  public enum TargetType {
    @EnumValue("Universe")
    Universe(KnownAlertLabels.UNIVERSE_UUID,
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
  public UUID uuid;

  @Enumerated(EnumType.STRING)
  public TargetType targetType;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  public String name;

  @Constraints.Required
  @Column(columnDefinition = "Text", nullable = false)
  public String query;

  @Constraints.Required
  public boolean isActive;

  @Constraints.Required
  @Column(nullable = false)
  public UUID customerUUID;

  @OneToMany(mappedBy = "definition", cascade = CascadeType.ALL, orphanRemoval = true)
  private List<AlertDefinitionLabel> labels;

  private static final Finder<UUID, AlertDefinition> find =
    new Finder<UUID, AlertDefinition>(AlertDefinition.class) {};

  public static AlertDefinition create(
    UUID customerUUID,
    TargetType targetType,
    String name,
    String query,
    boolean isActive,
    List<AlertDefinitionLabel> labels
  ) {
    AlertDefinition definition = new AlertDefinition();
    definition.uuid = UUID.randomUUID();
    definition.targetType = targetType;
    definition.name = name;
    definition.customerUUID = customerUUID;
    definition.query = query;
    definition.isActive = isActive;
    definition.setLabels(labels);
    definition.save();

    return definition;
  }

  public static AlertDefinition get(UUID alertDefinitionUUID) {
    return find.query()
      .fetch("labels")
      .where().idEq(alertDefinitionUUID)
      .findOne();
  }

  public static AlertDefinition getOrBadRequest(UUID alertDefinitionUUID) {
    AlertDefinition alertDefinition = get(alertDefinitionUUID);
    if (alertDefinition == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Definition UUID: "
          + alertDefinitionUUID);
    }
    return alertDefinition;
  }

  public static AlertDefinition get(UUID customerUUID, UUID universeUUID, String name) {
    return find.query()
      .fetch("labels")
      .where()
      .eq("customer_uuid", customerUUID)
      .eq("labels.key.name", KnownAlertLabels.UNIVERSE_UUID.labelName())
      .eq("labels.value", universeUUID.toString())
      .eq("name", name)
      .findOne();
  }

  public static AlertDefinition getOrBadRequest(UUID customerUUID, UUID universeUUID, String name) {
    AlertDefinition alertDefinition = get(customerUUID, universeUUID, name);
    if (alertDefinition == null) {
      throw new YWServiceException(BAD_REQUEST, "Could not find Alert Definition");
    }
    return alertDefinition;
  }

  public static List<AlertDefinition> get(UUID customerUUID, AlertDefinitionLabel label) {
    return find.query()
      .fetch("labels")
      .where()
      .eq("customer_uuid", customerUUID)
      .eq("labels.key.name", label.getName())
      .eq("labels.value", label.getValue())
      .findList();
  }

  public static void delete(UUID customerUUID, AlertDefinitionLabel label) {
    find.query()
      .fetch("labels")
      .where()
      .eq("customer_uuid", customerUUID)
      .eq("labels.key.name", label.getName())
      .eq("labels.value", label.getValue())
      .delete();
  }

  public static AlertDefinition update(
    UUID alertDefinitionUUID,
    String query,
    boolean isActive,
    List<AlertDefinitionLabel> labels
  ) {
    AlertDefinition alertDefinition = get(alertDefinitionUUID);
    alertDefinition.query = query;
    alertDefinition.isActive = isActive;
    alertDefinition.setLabels(labels);
    alertDefinition.save();

    return alertDefinition;
  }

  public static Set<AlertDefinition> listActive(UUID customerUUID) {
    return find.query()
      .fetch("labels")
      .where()
      .eq("customer_uuid", customerUUID)
      .eq("is_active", true)
      .findSet();
  }

  public List<AlertDefinitionLabel> getLabels() {
    return labels;
  }

  public List<AlertDefinitionLabel> getEffectiveLabels() {
    List<AlertDefinitionLabel> effectiveLabels = new ArrayList<>();
    effectiveLabels.add(new AlertDefinitionLabel(
      this, KnownAlertLabels.DEFINITION_UUID, uuid.toString()));
    effectiveLabels.add(new AlertDefinitionLabel(
      this, KnownAlertLabels.DEFINITION_NAME, name));
    effectiveLabels.add(new AlertDefinitionLabel(
      this, KnownAlertLabels.CUSTOMER_UUID, customerUUID.toString()));
    effectiveLabels.addAll(labels);
    return effectiveLabels;
  }

  public UUID getUniverseUUID() {
    return Optional.ofNullable(getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
      .map(UUID::fromString)
      .orElseThrow(() -> new IllegalStateException(
        "Definition " + uuid + " does not have universe UUID"));
  }

  public String getLabelValue(KnownAlertLabels knownLabel) {
    return getLabelValue(knownLabel.labelName());
  }

  public String getLabelValue(String name) {
    return getEffectiveLabels().stream()
      .filter(label -> name.equals(label.getName()))
      .map(AlertDefinitionLabel::getValue)
      .findFirst().orElse(null);
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
}
