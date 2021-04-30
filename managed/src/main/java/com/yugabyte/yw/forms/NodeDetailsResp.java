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
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.HashSet;
import java.util.Set;

public class NodeDetailsResp {

  @JsonUnwrapped
  @JsonIgnoreProperties("allowedActions")
  public final NodeDetails delegate;

  @JsonIgnore
  public final Universe universe;

  public NodeDetailsResp(NodeDetails nodeDetails, Universe universe) {
    this.delegate = nodeDetails;
    this.universe = universe;
  }

  @JsonProperty(access = JsonProperty.Access.READ_ONLY)
  public Set<NodeActionType> getAllowedActions() {
    Set<NodeActionType> allowedActions = new HashSet<>(delegate.getStaticAllowedActions());
    if (universe != null && delegate.state == NodeDetails.NodeState.Live && !delegate.isMaster
      && Util.areMastersUnderReplicated(delegate, universe)) {
      allowedActions.add(NodeActionType.START_MASTER);
    }
    return allowedActions;
  }
}
