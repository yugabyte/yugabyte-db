// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;

import com.yugabyte.yw.common.config.RuntimeConfGetter;

/**
 * Builds a {@link NodeUniverseManager} whose command builders can be exercised directly. The
 * injected fields it needs are package private, so tests living outside {@code
 * com.yugabyte.yw.common} cannot wire one up themselves.
 */
public final class TestNodeUniverseManagers {

  private TestNodeUniverseManagers() {}

  public static NodeUniverseManager spyWith(RuntimeConfGetter confGetter, String ybHomeDir) {
    NodeUniverseManager manager = spy(new NodeUniverseManager());
    manager.confGetter = confGetter;
    doReturn(ybHomeDir).when(manager).getYbHomeDir(any(), any());
    return manager;
  }
}
