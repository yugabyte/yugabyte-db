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
import io.swagger.annotations.ApiModelProperty;
import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Embeddable;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import play.libs.Json;

@EqualsAndHashCode
@Embeddable
public class KmsHistoryId implements Serializable {
  public enum TargetType {
    @EnumValue("UNIVERSE_KEY")
    UNIVERSE_KEY;
  }

  @Column(name = "key_ref")
  public String keyRef;

  @Column(name = "target_uuid")
  public UUID targetUuid;

  @Enumerated(EnumType.STRING)
  @Column(name = "type")
  public TargetType type;

  @Column(name = "re_encryption_count", nullable = false)
  @ApiModelProperty(value = "Number of times master key rotation was performed.")
  @Getter
  public int reEncryptionCount;

  public KmsHistoryId(String keyRef, UUID targetUuid, TargetType type, int reEncryptionCount) {
    this.keyRef = keyRef;
    this.targetUuid = targetUuid;
    this.type = type;
    this.reEncryptionCount = reEncryptionCount;
  }

  @Override
  public String toString() {
    return Json.newObject()
        .put("key_ref", keyRef)
        .put("target_uuid", targetUuid.toString())
        .put("type", type.name())
        .put("re_encryption_count", reEncryptionCount)
        .toString();
  }
}
