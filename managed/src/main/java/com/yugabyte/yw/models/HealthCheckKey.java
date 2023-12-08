// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import jakarta.persistence.Embeddable;
import jakarta.persistence.Entity;
import java.io.Serializable;
import java.util.Date;
import java.util.UUID;

@Entity
@Embeddable
public class HealthCheckKey implements Serializable {
  public UUID universeUUID;
  public Date checkTime;

  @Override
  public boolean equals(Object object) {
    if (object instanceof HealthCheckKey) {
      HealthCheckKey key = (HealthCheckKey) object;
      if (this.universeUUID.equals(key.universeUUID) && this.checkTime.equals(key.checkTime)) {
        return true;
      }
    }
    return false;
  }

  @Override
  public int hashCode() {
    return universeUUID.hashCode() + checkTime.hashCode();
  }

  public static HealthCheckKey create(UUID universeUUID) {
    HealthCheckKey key = new HealthCheckKey();
    key.universeUUID = universeUUID;
    key.checkTime = new Date();
    return key;
  }

  @Override
  public String toString() {
    return universeUUID + ":" + checkTime;
  }
}
