// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.AccessLevel;
import lombok.Builder;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.ToString;
import lombok.experimental.FieldDefaults;
import lombok.experimental.SuperBuilder;

/** Target of a {@link NodeUniverseManager} call. */
// Not @Value/@Builder: the class must be non-final with a SuperBuilder to be subclassed.
@Getter
@ToString
@EqualsAndHashCode
@FieldDefaults(makeFinal = true, level = AccessLevel.PRIVATE)
@SuperBuilder(toBuilder = true)
public class NodeUniverseContext {
  Universe universe;
  NodeDetails node;
  @Builder.Default ShellProcessContext shellProcessContext = NodeUniverseManager.DEFAULT_CONTEXT;

  // Updated user intent if it is not yet written to the universe. Read it via getUserIntent().
  @Getter(AccessLevel.NONE)
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

  /** The overriding user intent if set, else the one of the node's cluster. */
  public UserIntent getUserIntent() {
    return userIntent == null ? universe.getCluster(node.placementUuid).userIntent : userIntent;
  }

  public NodeUniverseContext withTimeout(long timeoutSecs) {
    return toBuilder()
        .shellProcessContext(getShellProcessContext().toBuilder().timeoutSecs(timeoutSecs).build())
        .build();
  }
}
