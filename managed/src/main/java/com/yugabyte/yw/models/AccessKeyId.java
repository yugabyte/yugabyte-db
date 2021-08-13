// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import java.io.Serializable;
import java.util.Objects;
import java.util.UUID;
import javax.persistence.Embeddable;
import javax.persistence.Entity;

@Entity
@Embeddable
public class AccessKeyId implements Serializable {
  public String keyCode;
  public UUID providerUUID;

  @Override
  public boolean equals(Object object) {
    if (object instanceof AccessKeyId) {
      AccessKeyId key = (AccessKeyId) object;
      return Objects.equals(this.keyCode, key.keyCode)
          && Objects.equals(this.providerUUID, key.providerUUID);
    }
    return false;
  }

  @Override
  public int hashCode() {
    return keyCode.hashCode() + providerUUID.hashCode();
  }

  public static AccessKeyId create(UUID providerUUID, String key_code) {
    AccessKeyId key = new AccessKeyId();
    key.keyCode = key_code;
    key.providerUUID = providerUUID;
    return key;
  }

  @Override
  public String toString() {
    return keyCode + ":" + providerUUID;
  }
}
