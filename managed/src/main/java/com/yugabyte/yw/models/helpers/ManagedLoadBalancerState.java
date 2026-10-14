// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonIgnore;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.Data;

/**
 * The load balancers that YBA created for a universe. A subtask records a load balancer before the
 * cloud call and removes it after the cloud delete, so the list names every load balancer that may
 * exist.
 */
@Data
public class ManagedLoadBalancerState {

  private List<ManagedLoadBalancer> loadBalancers = new ArrayList<>();

  public Optional<ManagedLoadBalancer> find(UUID clusterUuid, UUID regionUuid, String name) {
    return loadBalancers.stream()
        .filter(lb -> lb.matches(clusterUuid, regionUuid, name))
        .findFirst();
  }

  /** Adds the load balancer, or replaces the one with the same cluster, region and name. */
  public void put(ManagedLoadBalancer lb) {
    remove(lb.getClusterUuid(), lb.getRegionUuid(), lb.getName());
    loadBalancers.add(lb);
  }

  public void remove(UUID clusterUuid, UUID regionUuid, String name) {
    loadBalancers.removeIf(lb -> lb.matches(clusterUuid, regionUuid, name));
  }

  @JsonIgnore
  public boolean isEmpty() {
    return loadBalancers.isEmpty();
  }
}
