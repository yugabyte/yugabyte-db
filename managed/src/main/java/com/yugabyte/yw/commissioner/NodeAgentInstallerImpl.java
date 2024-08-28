// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.NodeAgentEnabler.NodeAgentInstaller;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;

public class NodeAgentInstallerImpl implements NodeAgentInstaller {

  @Override
  public boolean install(UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails) {
    // TODO Not yet implemented.
    return false;
  }

  @Override
  public boolean reinstall(
      UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails, NodeAgent nodeAgent) {
    // TODO Not yet implemented.
    return false;
  }

  @Override
  public boolean migrate(UUID customerUuid, UUID universeUuid) {
    // TODO Not yet implemented.
    return false;
  }
}
