// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import javax.persistence.Embeddable;
import javax.persistence.Entity;
import java.io.Serializable;

@Entity
@Embeddable
public class PriceComponentKey implements Serializable {

  // Code for the provider that this PriceComponent belongs to (e.g. "aws")
  public String providerCode;

  // Code for the region that this PriceComponent belongs to (e.g. "us-west-2")
  public String regionCode;

  // Code to identify this particular component (e.g. "m3.medium", "io1.size", etc.)
  public String componentCode;

  @Override
  public boolean equals(Object object) {
    if (object instanceof PriceComponentKey) {
      PriceComponentKey key = (PriceComponentKey) object;
      return (this.providerCode.equals(key.providerCode)
              && this.regionCode.equals(key.regionCode)
              && this.componentCode.equals(key.componentCode));
    }
    return false;
  }

  @Override
  public int hashCode() {
    return providerCode.hashCode() + regionCode.hashCode() + componentCode.hashCode();
  }

  public static PriceComponentKey create(String providerCode, String regionCode,
                                         String componentCode) {
    PriceComponentKey key = new PriceComponentKey();
    key.providerCode = providerCode;
    key.regionCode = regionCode;
    key.componentCode = componentCode;
    return key;
  }

  @Override
  public String toString() {
    return providerCode + "/" + regionCode + ":" + componentCode;
  }
}
