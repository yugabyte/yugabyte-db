// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.filters.NodeAgentFilter;
import java.util.Set;
import java.util.UUID;
import lombok.Data;
import org.apache.commons.collections.CollectionUtils;

@Data
public class NodeAgentApiFilter {
  private Set<String> nodeIps;
  private CloudType cloudType;
  private UUID providerUuid;
  private UUID universeUuid;
  private UUID regionUuid;
  private UUID zoneUuid;

  public NodeAgentFilter toFilter() {
    NodeAgentFilter.NodeAgentFilterBuilder builder = NodeAgentFilter.builder();
    if (CollectionUtils.isNotEmpty(nodeIps)) {
      builder.nodeIps(nodeIps);
    }
    if (cloudType != null) {
      builder.cloudType(cloudType);
    }
    if (providerUuid != null) {
      builder.providerUuid(providerUuid);
    }
    if (universeUuid != null) {
      builder.universeUuid(universeUuid);
    }
    if (regionUuid != null) {
      builder.regionUuid(regionUuid);
    }
    if (zoneUuid != null) {
      builder.zoneUuid(zoneUuid);
    }
    return builder.build();
  }
}
