package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.AvailabilityZone;
import lombok.Data;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

@Data
public class LoadBalancerConfig {
  private String lbName;
  private Map<AvailabilityZone, Set<NodeDetails>> azNodes;

  public LoadBalancerConfig(String lbName) {
    this.lbName = lbName;
    this.azNodes = new HashMap<>();
  }

  public LoadBalancerConfig(String lbName, Map<AvailabilityZone, Set<NodeDetails>> azNodes) {
    this.lbName = lbName;
    this.azNodes = azNodes;
  }

  public void addNodes(AvailabilityZone az, Set<NodeDetails> nodes) {
    azNodes.computeIfAbsent(az, k -> new HashSet<>()).addAll(nodes);
  }
}
