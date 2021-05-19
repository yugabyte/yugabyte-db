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
public class AlertDefinitionLabelKey extends Model {

  @JsonIgnore
  private UUID definitionUUID;

  private String name;

  public UUID getDefinitionUUID() {
    return definitionUUID;
  }

  public void setDefinitionUUID(UUID definitionUUID) {
    this.definitionUUID = definitionUUID;
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
    AlertDefinitionLabelKey that = (AlertDefinitionLabelKey) o;
    return Objects.equals(definitionUUID, that.definitionUUID) && Objects.equals(name, that.name);
  }

  @Override
  public int hashCode() {
    return Objects.hash(definitionUUID, name);
  }

  @Override
  public String toString() {
    return "AlertDefinitionLabelKey{" +
      "definitionUUID=" + definitionUUID +
      ", name='" + name + '\'' +
      '}';
  }
}
