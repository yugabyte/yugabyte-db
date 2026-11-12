// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.mockito.ArgumentMatchers.argThat;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Objects;

/** Mockito matchers for {@link NodeUniverseContext} arguments of {@link NodeUniverseManager}. */
public final class NodeUniverseContextMatchers {

  private NodeUniverseContextMatchers() {}

  /** Matches a context for the given node and universe; a null argument matches anything. */
  public static NodeUniverseContext nodeContext(NodeDetails node, Universe universe) {
    return nodeContext(node, universe, null);
  }

  /**
   * Matches a context for the given node, universe and shell context; a null argument matches
   * anything.
   */
  public static NodeUniverseContext nodeContext(
      NodeDetails node, Universe universe, ShellProcessContext shellProcessContext) {
    return argThat(
        c ->
            c != null
                && (node == null || Objects.equals(node, c.getNode()))
                && (universe == null || Objects.equals(universe, c.getUniverse()))
                && (shellProcessContext == null
                    || Objects.equals(shellProcessContext, c.getShellProcessContext())));
  }

  public static NodeUniverseContext forNode(NodeDetails node, Universe universe) {
    return argThat(c -> c.getNode() == node && c.getUniverse() == universe);
  }

  public static NodeUniverseYsqlContext ysqlContext(NodeDetails node, Universe universe) {
    return argThat(c -> c.getNode() == node && c.getUniverse() == universe);
  }

  public static NodeUniverseYsqlContext ysqlContext(
      NodeDetails node,
      Universe universe,
      boolean authEnabled,
      boolean cpEnabled,
      long timeOutSec,
      boolean logCmdOutput) {
    return argThat(
        c ->
            c.getNode() == node
                && c.getUniverse() == universe
                && c.isCpEnabled() == cpEnabled
                && c.isAuthEnabled() == authEnabled
                && c.getShellProcessContext().getTimeoutSecs() == timeOutSec
                && c.getShellProcessContext().isLogCmdOutput() == logCmdOutput);
  }
}
