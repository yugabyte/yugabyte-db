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

import javax.persistence.Embeddable;
import java.util.Objects;
import java.util.UUID;

@Embeddable
public class AlertLabelKey extends Model {

  @JsonIgnore private UUID alertUUID;

  private String name;

  public UUID getAlertUUID() {
    return alertUUID;
  }

  public void setAlertUUID(UUID alertUUID) {
    this.alertUUID = alertUUID;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    AlertLabelKey that = (AlertLabelKey) o;
    return Objects.equals(alertUUID, that.alertUUID) && Objects.equals(name, that.name);
  }

  @Override
  public int hashCode() {
    return Objects.hash(alertUUID, name);
  }

  @Override
  public String toString() {
    return "AlertLabelKey{" + "alertUUID=" + alertUUID + ", name='" + name + '\'' + '}';
  }
}
