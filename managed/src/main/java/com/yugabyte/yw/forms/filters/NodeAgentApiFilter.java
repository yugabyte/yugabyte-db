// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.filters;

import com.yugabyte.yw.models.filters.NodeAgentFilter;
import java.util.List;
import lombok.Data;
import org.apache.commons.collections.CollectionUtils;

@Data
public class NodeAgentApiFilter {
  private List<String> nodeIps;

  // Convert to the filter for model.
  public NodeAgentFilter toFilter() {
    NodeAgentFilter.NodeAgentFilterBuilder builder = NodeAgentFilter.builder();
    if (!CollectionUtils.isEmpty(nodeIps)) {
      builder.nodeIps(nodeIps);
    }
    return builder.build();
  }
}
