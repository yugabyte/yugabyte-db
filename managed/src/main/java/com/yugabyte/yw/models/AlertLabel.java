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
import com.yugabyte.yw.models.helpers.UniqueKeyListValue;
import io.ebean.Model;
import jakarta.persistence.Column;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import jakarta.persistence.ManyToOne;
import java.util.Objects;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
@ToString
public class AlertLabel extends Model implements UniqueKeyListValue<AlertLabel> {

  @EmbeddedId private AlertLabelKey key;

  @Column(nullable = false)
  private String value;

  @ManyToOne @JsonIgnore @EqualsAndHashCode.Exclude @ToString.Exclude private Alert alert;

  public AlertLabel() {
    this.key = new AlertLabelKey();
  }

  public AlertLabel(String name, String value) {
    this();
    key.setName(name);
    this.value = value;
  }

  public AlertLabel(Alert definition, String name, String value) {
    this(name, value);
    setAlert(definition);
  }

  public String getName() {
    return key.getName();
  }

  public void setAlert(Alert alert) {
    this.alert = alert;
    key.setAlertUUID(alert.getUuid());
  }

  @Override
  @JsonIgnore
  public boolean keyEquals(AlertLabel other) {
    return Objects.equals(getName(), other.getName());
  }

  @Override
  @JsonIgnore
  public boolean valueEquals(AlertLabel other) {
    return keyEquals(other) && Objects.equals(getValue(), other.getValue());
  }
}
