// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.filters;

import java.util.List;
import lombok.Builder;
import lombok.Getter;

@Builder
@Getter
public class NodeAgentFilter {
  private List<String> nodeIps;
}
