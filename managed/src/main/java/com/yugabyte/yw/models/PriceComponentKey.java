// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import jakarta.persistence.Embeddable;
import jakarta.persistence.Entity;
import java.io.Serializable;
import java.util.UUID;
import lombok.Data;

@Data
@Entity
@Embeddable
public class PriceComponentKey implements Serializable {

  // UUID for the provider that this PriceComponent belongs to
  public UUID providerUuid;

  // Code for the region that this PriceComponent belongs to (e.g. "us-west-2")
  public String regionCode;

  // Code to identify this particular component (e.g. "m3.medium", "io1.size", etc.)
  public String componentCode;

  public static PriceComponentKey create(
      UUID providerUuid, String regionCode, String componentCode) {
    PriceComponentKey key = new PriceComponentKey();
    key.providerUuid = providerUuid;
    key.regionCode = regionCode;
    key.componentCode = componentCode;
    return key;
  }

  @Override
  public String toString() {
    return providerUuid.toString() + "/" + regionCode + ":" + componentCode;
  }
}
