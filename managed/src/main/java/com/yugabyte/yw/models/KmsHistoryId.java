/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import io.ebean.annotation.EnumValue;
import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Embeddable;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import lombok.Data;
import play.libs.Json;

@Embeddable
@Data
public class KmsHistoryId implements Serializable {

  public enum TargetType {
    @EnumValue("UNIVERSE_KEY")
    UNIVERSE_KEY;
  }

  @Column(name = "key_ref")
  private String keyRef;

  @Column(name = "target_uuid")
  private UUID targetUuid;

  @Enumerated(EnumType.STRING)
  @Column(name = "type")
  private TargetType type;

  public KmsHistoryId(String keyRef, UUID targetUuid, TargetType type) {
    this.setKeyRef(keyRef);
    this.setTargetUuid(targetUuid);
    this.setType(type);
  }

  @Override
  public String toString() {
    return Json.newObject()
        .put("key_ref", getKeyRef())
        .put("target_uuid", getTargetUuid().toString())
        .put("type", getType().name())
        .toString();
  }

  @Override
  public boolean equals(Object o) {
    if (!(o instanceof KmsHistoryId)) return false;
    KmsHistoryId oCast = (KmsHistoryId) o;
    return oCast.getKeyRef().equals(this.getKeyRef())
        && oCast.getTargetUuid().equals(this.getTargetUuid())
        && oCast.getType().equals(this.getType());
  }

  @Override
  public int hashCode() {
    int result = 11;
    result = 31 * result + getKeyRef().hashCode();
    result = 31 * result + getTargetUuid().hashCode();
    result = 31 * result + getType().hashCode();
    return result;
  }
}
