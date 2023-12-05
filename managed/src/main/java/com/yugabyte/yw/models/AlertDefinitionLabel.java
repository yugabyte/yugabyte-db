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
import jakarta.persistence.Column;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import jakarta.persistence.ManyToOne;
import java.util.Objects;
import javax.validation.constraints.NotNull;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Entity
@Data
@EqualsAndHashCode(exclude = "definition", callSuper = false)
@ToString(exclude = "definition")
public class AlertDefinitionLabel extends Model
    implements UniqueKeyListValue<AlertDefinitionLabel> {

  @NotNull @EmbeddedId private AlertDefinitionLabelKey key;

  @Column(nullable = false)
  @NotNull
  private String value;

  @ManyToOne @JsonIgnore private AlertDefinition definition;

  public AlertDefinitionLabel() {
    this.key = new AlertDefinitionLabelKey();
  }

  public AlertDefinitionLabel(String name, String value) {
    this();
    key.setName(name);
    this.value = value;
  }

  public AlertDefinitionLabel(KnownAlertLabels knownLabel, String value) {
    this(knownLabel.labelName(), value);
  }

  public AlertDefinitionLabel(AlertDefinition definition, String name, String value) {
    this(name, value);
    setDefinition(definition);
  }

  public AlertDefinitionLabel(
      AlertDefinition definition, KnownAlertLabels knownLabel, String value) {
    this(definition, knownLabel.labelName(), value);
  }

  public String getName() {
    return key.getName();
  }

  public void setDefinition(AlertDefinition definition) {
    this.definition = definition;
    key.setDefinitionUUID(definition.getUuid());
  }

  @Override
  @JsonIgnore
  public boolean keyEquals(AlertDefinitionLabel other) {
    return Objects.equals(getName(), other.getName());
  }

  @Override
  @JsonIgnore
  public boolean valueEquals(AlertDefinitionLabel other) {
    return keyEquals(other) && Objects.equals(getValue(), other.getValue());
  }
}
