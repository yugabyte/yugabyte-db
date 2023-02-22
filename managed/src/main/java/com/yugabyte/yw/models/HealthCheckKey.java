// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.io.Serializable;
import java.util.Date;
import java.util.UUID;
import javax.persistence.Embeddable;
import javax.persistence.Entity;
import lombok.Data;

@Entity
@Embeddable
@Data
public class HealthCheckKey implements Serializable {
  private UUID universeUUID;
  private Date checkTime;

  public static HealthCheckKey create(UUID universeUUID) {
    HealthCheckKey key = new HealthCheckKey();
    key.setUniverseUUID(universeUUID);
    key.setCheckTime(new Date());
    return key;
  }
}
