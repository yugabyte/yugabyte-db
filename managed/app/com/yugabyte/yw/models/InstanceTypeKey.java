// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import java.io.Serializable;

import javax.persistence.Embeddable;
import javax.persistence.Entity;

@Entity
@Embeddable
public class InstanceTypeKey implements Serializable {
  public String providerCode;
  public String instanceTypeCode;

  @Override
  public boolean equals(Object object) {
    if(object instanceof InstanceTypeKey) {
      InstanceTypeKey key = (InstanceTypeKey) object;
      if(this.providerCode == key.providerCode && this.instanceTypeCode == key.instanceTypeCode) {
        return true;
      }
    }
    return false;
  }

  @Override
  public int hashCode() {
    return providerCode.hashCode() + instanceTypeCode.hashCode();
  }

  public static InstanceTypeKey create(String instanceTypeCode, String providerCode) {
    InstanceTypeKey key = new InstanceTypeKey();
    key.providerCode = providerCode;
    key.instanceTypeCode = instanceTypeCode;
    return key;
  }

  @Override
  public String toString() {
    return providerCode + ":" + instanceTypeCode;
  }
}
