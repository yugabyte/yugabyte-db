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
import io.ebean.Model;

import javax.persistence.*;
import java.util.Objects;

@Entity
public class AlertLabel extends Model {

  @EmbeddedId private AlertLabelKey key;

  @Column(nullable = false)
  private String value;

  @ManyToOne
  @MapsId("alert")
  @JsonIgnore
  private Alert alert;

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

  public Alert getAlert() {
    return alert;
  }

  public void setAlert(Alert alert) {
    this.alert = alert;
    key.setAlertUUID(alert.uuid);
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
    AlertLabel label = (AlertLabel) o;
    return Objects.equals(key, label.key) && Objects.equals(value, label.value);
  }

  @Override
  public int hashCode() {
    return Objects.hash(key, value);
  }

  @Override
  public String toString() {
    return "AlertLabel{" + "key=" + key + ", value=" + value + '}';
  }
}
