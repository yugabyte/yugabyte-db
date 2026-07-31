// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.Builder;
import lombok.Value;

/** Target of a {@link NodeUniverseManager} call. */
@Value
@Builder(toBuilder = true)
public class NodeUniverseContext {
  Universe universe;
  NodeDetails node;

  @Builder.Default ShellProcessContext shellProcessContext = NodeUniverseManager.DEFAULT_CONTEXT;

  // Updated user intent if it is not yet written to the universe.
  UserIntent userIntent;

  public static NodeUniverseContext of(Universe universe, NodeDetails node) {
    return builder().universe(universe).node(node).build();
  }

  public static NodeUniverseContext of(
      Universe universe, NodeDetails node, ShellProcessContext shellProcessContext) {
    return builder().universe(universe).node(node).shellProcessContext(shellProcessContext).build();
  }

  public static NodeUniverseContext of(
      Universe universe,
      NodeDetails node,
      ShellProcessContext shellProcessContext,
      UserIntent userIntent) {
    return builder()
        .universe(universe)
        .node(node)
        .shellProcessContext(shellProcessContext)
        .userIntent(userIntent)
        .build();
  }
}
