package com.yugabyte.yw.models.helpers;

import java.util.UUID;
import lombok.Data;

@Data
public class LoadBalancerPlacement {
  private UUID providerUUID;
  private String regionCode;
  private String lbName;

  public LoadBalancerPlacement(UUID providerUUID, String regionCode, String lbName) {
    this.providerUUID = providerUUID;
    this.regionCode = regionCode;
    this.lbName = lbName;
  }
}
