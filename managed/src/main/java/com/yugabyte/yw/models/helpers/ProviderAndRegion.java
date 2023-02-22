// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Region;
import java.io.Serializable;
import java.util.UUID;
import lombok.Value;

@Value
public class ProviderAndRegion implements Serializable {
  UUID providerUuid;
  String regionCode;

  public static ProviderAndRegion from(Region region) {
    return new ProviderAndRegion(region.getProvider().getUuid(), region.getCode());
  }

  public static ProviderAndRegion from(PriceComponent priceComponent) {
    return new ProviderAndRegion(priceComponent.getProviderUuid(), priceComponent.getRegionCode());
  }
}
