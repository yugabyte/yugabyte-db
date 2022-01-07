// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.ToString;

@EqualsAndHashCode
@ToString
@Getter
@Builder
public class NodeStatus {
  private final NodeState nodeState;
  private final MasterState masterState;

  private NodeStatus(NodeState nodeState, MasterState masterState) {
    this.nodeState = nodeState;
    this.masterState = masterState;
  }

  public static NodeStatus fromNode(NodeDetails node) {
    return NodeStatus.builder().nodeState(node.state).masterState(node.masterState).build();
  }

  public void fillNodeStates(NodeDetails node) {
    if (nodeState != null) {
      node.state = nodeState;
    }
    if (masterState != null) {
      node.masterState = (masterState == MasterState.None) ? null : masterState;
    }
  }

  // Compares the given state with this node status ignoring null fields of the given node status.
  // This is useful when only some states need to be checked.
  public boolean equalsIgnoreNull(NodeStatus nodeStatus) {
    if (nodeStatus == null) {
      return false;
    }
    if (nodeStatus.nodeState != null && nodeState != nodeStatus.nodeState) {
      return false;
    }
    if (nodeStatus.masterState != null && masterState != nodeStatus.masterState) {
      return false;
    }
    return true;
  }
}
