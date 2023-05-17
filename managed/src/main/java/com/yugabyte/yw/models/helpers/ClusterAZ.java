package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.AvailabilityZone;
import java.util.UUID;
import lombok.Data;

@Data
public class ClusterAZ {
  private UUID clusterUUID;
  private AvailabilityZone az;

  public ClusterAZ(UUID clusterUUID, AvailabilityZone az) {
    this.clusterUUID = clusterUUID;
    this.az = az;
  }
}
