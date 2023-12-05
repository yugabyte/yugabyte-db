// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import jakarta.persistence.Embeddable;
import java.io.Serializable;
import java.util.UUID;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@Embeddable
public class InstanceTypeKey implements Serializable {
  private UUID providerUuid;
  private String instanceTypeCode;

  @Override
  public boolean equals(Object object) {
    if (object instanceof InstanceTypeKey) {
      InstanceTypeKey key = (InstanceTypeKey) object;
      return this.providerUuid.equals(key.providerUuid)
          && this.instanceTypeCode.equals(key.instanceTypeCode);
    }
    return false;
  }

  @Override
  public int hashCode() {
    return providerUuid.hashCode() + instanceTypeCode.hashCode();
  }

  public static InstanceTypeKey create(String instanceTypeCode, UUID providerUuid) {
    InstanceTypeKey key = new InstanceTypeKey();
    key.providerUuid = providerUuid;
    key.instanceTypeCode = instanceTypeCode;
    return key;
  }

  @Override
  public String toString() {
    return providerUuid + ":" + instanceTypeCode;
  }
}
