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

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.AllowedActionsHelper;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;

public class NodeDetailsResp {

  @JsonUnwrapped public final NodeDetails delegate;

  @JsonIgnore public final Universe universe;

  // Applicable to kubernetes universes only.
  public String kubernetesOverrides;

  public NodeDetailsResp(NodeDetails nodeDetails, Universe universe) {
    this(nodeDetails, universe, "");
  }

  public NodeDetailsResp(NodeDetails nodeDetails, Universe universe, String helmValues) {
    this.delegate = nodeDetails;
    this.universe = universe;
    this.kubernetesOverrides = helmValues;
  }

  @JsonProperty(access = JsonProperty.Access.READ_ONLY)
  public Set<NodeActionType> getAllowedActions() {
    return new AllowedActionsHelper(universe, delegate).listAllowedActions();
  }
}
