// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.commissioner.Common.CloudType;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.Getter;

@Builder
@Getter
public class NodeAgentFilter {
  private Set<String> nodeIps;
  private CloudType cloudType;
  private UUID providerUuid;
  private UUID universeUuid;
  private UUID regionUuid;
  private UUID zoneUuid;
}
