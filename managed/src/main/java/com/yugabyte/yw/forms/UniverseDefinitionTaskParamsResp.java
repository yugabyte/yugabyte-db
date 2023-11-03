/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.Universe;
import java.util.Set;
import java.util.stream.Collectors;

public class UniverseDefinitionTaskParamsResp {

  @JsonUnwrapped
  @JsonIgnoreProperties({"nodeDetailsSet"})
  public final UniverseDefinitionTaskParams delegate;

  private final Set<NodeDetailsResp> nodeDetailsSet;

  public UniverseDefinitionTaskParamsResp(
      UniverseDefinitionTaskParams universeDefinitionTaskParams, Universe universe) {
    this.delegate = universeDefinitionTaskParams;
    if (universeDefinitionTaskParams.nodeDetailsSet == null) {
      nodeDetailsSet = null;
    } else {
      nodeDetailsSet =
          universeDefinitionTaskParams.nodeDetailsSet.stream()
              .map(nodeDetails -> new NodeDetailsResp(nodeDetails, universe))
              .collect(Collectors.toSet());
    }
  }

  @JsonProperty(access = JsonProperty.Access.READ_ONLY)
  public Set<NodeDetailsResp> getNodeDetailsSet() {
    return nodeDetailsSet;
  }
}
