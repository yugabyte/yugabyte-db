// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import javax.persistence.Embeddable;
import javax.persistence.Entity;
import java.io.Serializable;

@Entity
@Embeddable
public class InstanceTypeKey implements Serializable {
  public String providerCode;
  public String instanceTypeCode;

  public boolean equals(Object object) {
    if(object instanceof InstanceTypeKey) {
      InstanceTypeKey key = (InstanceTypeKey) object;
      if(this.providerCode == key.providerCode && this.instanceTypeCode == key.instanceTypeCode) {
        return true;
      }
    }
    return false;
  }

  public int hashCode() {
    return providerCode.hashCode() + instanceTypeCode.hashCode();
  }

  public static InstanceTypeKey create(String instanceTypeCode, String providerCode) {
    InstanceTypeKey key = new InstanceTypeKey();
    key.providerCode = providerCode;
    key.instanceTypeCode = instanceTypeCode;
    return key;
  }
}
