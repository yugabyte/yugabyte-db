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
import io.ebean.Model;

import javax.persistence.*;
import java.util.Objects;

@Entity
public class AlertDefinitionLabel extends Model {

  @EmbeddedId private AlertDefinitionLabelKey key;

  @Column(nullable = false)
  private String value;

  @ManyToOne
  @MapsId("definition")
  @JsonIgnore
  private AlertDefinition definition;

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

  public AlertDefinition getDefinition() {
    return definition;
  }

  public void setDefinition(AlertDefinition definition) {
    this.definition = definition;
    key.setDefinitionUUID(definition.getUuid());
  }

  public String getName() {
    return key.getName();
  }

  public void setName(String name) {
    key.setName(name);
  }

  public String getValue() {
    return value;
  }

  public void setValue(String value) {
    this.value = value;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AlertDefinitionLabel label = (AlertDefinitionLabel) o;
    return Objects.equals(key, label.key) && Objects.equals(value, label.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(key, value);
  }

  @Override
  public String toString() {
    return "AlertDefinitionLabel{" + "key=" + key + ", value=" + value + '}';
  }
}
