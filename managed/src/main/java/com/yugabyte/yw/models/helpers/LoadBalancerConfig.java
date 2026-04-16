package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.AvailabilityZone;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;

@Data
public class LoadBalancerConfig {
  private String lbName;
  private Map<AvailabilityZone, Set<NodeDetails>> azNodes;

  public LoadBalancerConfig(String lbName) {
    this.lbName = lbName;
    this.azNodes = new HashMap<>();
  }

  public void addNodes(AvailabilityZone az, Set<NodeDetails> nodes) {
    if (CollectionUtils.isNotEmpty(nodes)) {
      azNodes.computeIfAbsent(az, k -> new HashSet<>()).addAll(nodes);
    }
  }

  public void addAll(Map<AvailabilityZone, Set<NodeDetails>> otherAzNodes) {
    if (MapUtils.isNotEmpty(otherAzNodes)) {
      otherAzNodes.forEach(
          (key, value) ->
              azNodes.merge(
                  key,
                  value,
                  (v1, v2) -> {
                    v1.addAll(v2);
                    return v1;
                  }));
    }
  }
}
