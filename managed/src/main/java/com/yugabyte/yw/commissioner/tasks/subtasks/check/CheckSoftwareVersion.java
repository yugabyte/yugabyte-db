// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class CheckSoftwareVersion extends ServerSubTaskBase {

  @Inject
  protected CheckSoftwareVersion(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public String requiredVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    String address = node.cloudInfo.private_ip;
    try (YBClient client = getClient()) {
      if (node.isMaster) {
        checkSoftwareVersion(client, node, address, node.masterRpcPort);
      }
      if (node.isTserver) {
        checkSoftwareVersion(client, node, address, node.tserverRpcPort);
      }
    } catch (Exception e) {
      log.error("Error while fetching version info on node: " + node.nodeName, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkSoftwareVersion(YBClient client, NodeDetails node, String address, int port) {
    Optional<String> versionInfo = ybService.getServerVersion(client, address, port);
    if (!versionInfo.isPresent()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not determine version on node " + address);
    }

    String version = versionInfo.get();
    log.debug("Found version {} on node: {}, port: {}", version, node, port);

    // Validate the software version.
    if (Util.compareYbVersions(version, taskParams().requiredVersion) != 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Expected version: "
              + taskParams().requiredVersion
              + " but found: "
              + versionInfo.get()
              + " on node: "
              + node.nodeName);
    } else {
      log.debug("Validate software version on node {} port {}", node.cloudInfo.private_ip, port);
    }
  }
}
