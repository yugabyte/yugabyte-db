// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.AccessLevel;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.ToString;
import lombok.experimental.FieldDefaults;
import lombok.experimental.SuperBuilder;

/** Target of a YSQL command run through {@link NodeUniverseManager}. */
@Getter
@ToString(callSuper = true)
@EqualsAndHashCode(callSuper = true)
@FieldDefaults(makeFinal = true, level = AccessLevel.PRIVATE)
@SuperBuilder(toBuilder = true)
public class NodeUniverseYsqlContext extends NodeUniverseContext {

  // Null means "derive from the user intent"; @Builder.Default cannot read other fields.
  @Getter(AccessLevel.NONE)
  Boolean authEnabled;

  @Getter(AccessLevel.NONE)
  Boolean cpEnabled;

  public static NodeUniverseYsqlContext of(Universe universe, NodeDetails node) {
    return builder()
        .universe(universe)
        .node(node)
        .shellProcessContext(
            ShellProcessContext.DEFAULT.toBuilder()
                .logCmdOutput(true) // True by default.
                .build())
        .build();
  }

  public boolean isAuthEnabled() {
    return authEnabled != null ? authEnabled : getUserIntent().isYSQLAuthEnabled();
  }

  public boolean isCpEnabled() {
    return cpEnabled != null ? cpEnabled : getUserIntent().enableConnectionPooling;
  }

  public boolean isLogCmdOutput() {
    return getShellProcessContext().isLogCmdOutput();
  }

  public NodeUniverseYsqlContext withTimeout(long timeoutSec) {
    return toBuilder()
        .shellProcessContext(getShellProcessContext().toBuilder().timeoutSecs(timeoutSec).build())
        .build();
  }

  public NodeUniverseYsqlContext withLogCmdOutput(boolean logOutput) {
    return toBuilder()
        .shellProcessContext(getShellProcessContext().toBuilder().logCmdOutput(logOutput).build())
        .build();
  }
}
