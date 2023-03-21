// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;
import org.yb.VersionInfo;
import org.yb.client.GetStatusResponse;
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
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    String address = node.cloudInfo.private_ip;
    VersionInfo.VersionInfoPB versionInfo;
    try {
      if (node.isMaster) {
        versionInfo = getServerSoftwareVersion(universe, address, node.masterRpcPort);
      } else {
        versionInfo = getServerSoftwareVersion(universe, address, node.tserverRpcPort);
      }
    } catch (Exception e) {
      log.error("Error while fetching version info on node: " + node.nodeName, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }

    String serverVersion = versionInfo.getVersionNumber() + "-b" + versionInfo.getBuildNumber();

    // Validate the software version.
    if (Util.compareYbVersions(serverVersion, taskParams().requiredVersion) != 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Expected version: "
              + taskParams().requiredVersion
              + " but found: "
              + serverVersion
              + " on node: "
              + node.nodeName);
    }
  }

  private VersionInfo.VersionInfoPB getServerSoftwareVersion(
      Universe universe, String address, int port) throws Exception {
    try (YBClient client = getClient()) {
      GetStatusResponse response = client.getStatus(address, port);
      return response.getStatus().getVersionInfo();
    }
  }
}
