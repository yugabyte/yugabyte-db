// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models.helpers;

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

  private NodeStatus(NodeState nodeState) {
    this.nodeState = nodeState;
  }

  public static NodeStatus fromNode(NodeDetails node) {
    return NodeStatus.builder().nodeState(node.state).build();
  }

  public void fillNodeStates(NodeDetails node) {
    if (nodeState != null) {
      node.state = nodeState;
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
    return true;
  }
}
