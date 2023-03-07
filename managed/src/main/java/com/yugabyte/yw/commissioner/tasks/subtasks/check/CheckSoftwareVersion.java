// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class CheckSoftwareVersion extends ServerSubTaskBase {

  private final ApiHelper apiHelper;

  @Inject
  protected CheckSoftwareVersion(BaseTaskDependencies baseTaskDependencies, ApiHelper apiHelper) {
    super(baseTaskDependencies);
    this.apiHelper = apiHelper;
  }

  public static class Params extends ServerSubTaskParams {
    public String requiredVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static class VersionInfo {
    public String versionNumber;
    public String buildNumber;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    NodeDetails node = universe.getNodeOrBadRequest(taskParams().nodeName);
    String address = node.cloudInfo.private_ip;
    VersionInfo versionInfo;
    if (node.isMaster) {
      versionInfo = getServerSoftwareVersion(address, node.masterHttpPort);
    } else {
      versionInfo = getServerSoftwareVersion(address, node.tserverHttpPort);
    }

    String serverVersion = versionInfo.versionNumber + "-b" + versionInfo.buildNumber;

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

  private VersionInfo getServerSoftwareVersion(String address, int port) {
    String url = String.format("http://%s:%s/api/v1/version", address, port);
    JsonNode resp = apiHelper.getRequest(url);
    VersionInfo info = new VersionInfo();
    if (resp.has("version_number")) {
      info.versionNumber = resp.get("version_number").asText();
    } else {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not find version number on address " + address);
    }
    if (resp.has("build_number")) {
      info.buildNumber = resp.get("build_number").asText();
    } else {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not find build number on address " + address);
    }
    return info;
  }
}
