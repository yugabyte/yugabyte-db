// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

/**
 * A load balancer that YBA created for one cluster. It serves a set of zones in one region. The
 * cluster, region and name identify it; the zones can change, for example when the provider gets a
 * zone.
 */
@Data
@NoArgsConstructor
@AllArgsConstructor
public class ManagedLoadBalancer {

  /** Who can reach the load balancer. The v2 API returns it as ManagedLoadBalancerScheme. */
  public enum Scheme {
    PRIVATE,
    // Reserved. ManagedLoadBalancerUtil.planLoadBalancers does not plan one yet.
    PUBLIC
  }

  private UUID clusterUuid;
  private UUID regionUuid;
  private Scheme scheme;
  // A node registers with the load balancer that serves its zone. Today every active zone of the
  // region; ManagedLoadBalancerUtil.planLoadBalancers decides.
  private List<UUID> azUuids = new ArrayList<>();
  private String name;
  // A DNS name on AWS, an IP address on GCP and Azure. Null until the cloud call succeeds.
  private String address;

  public boolean matches(UUID clusterUuid, UUID regionUuid, String name) {
    return this.clusterUuid.equals(clusterUuid)
        && this.regionUuid.equals(regionUuid)
        && this.name.equals(name);
  }

  public boolean matches(ManagedLoadBalancer other) {
    return matches(other.clusterUuid, other.regionUuid, other.name);
  }
}
