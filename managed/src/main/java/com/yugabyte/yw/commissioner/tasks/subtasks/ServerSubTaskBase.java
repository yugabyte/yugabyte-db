/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public abstract class ServerSubTaskBase extends AbstractTaskBase {

  @Inject
  protected ServerSubTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ServerSubTaskParams taskParams() {
    return (ServerSubTaskParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", "
        + taskParams().nodeName
        + ", type="
        + taskParams().serverType
        + ")";
  }

  public String getMasterAddresses() {
    return getMasterAddresses(false);
  }

  public String getMasterAddresses(boolean getSecondary) {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    return universe.getMasterAddresses(false /* mastersQueryable */, getSecondary);
  }

  public HostAndPort getHostPort() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);
    return HostAndPort.fromParts(
        node.cloudInfo.private_ip,
        taskParams().serverType == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
  }

  public YBClient getClient() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    return ybService.getClient(masterAddresses, certificate);
  }

  public void closeClient(YBClient client) {
    ybService.closeClient(client, getMasterAddresses());
  }

  public void checkParams() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterAddresses = universe.getMasterAddresses();
    log.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalArgumentException(
          "Invalid master addresses " + masterAddresses + " for " + taskParams().getUniverseUUID());
    }

    NodeDetails node = universe.getNode(taskParams().nodeName);

    if (node == null) {
      throw new IllegalArgumentException(
          "Node "
              + taskParams().nodeName
              + " not found in universe "
              + taskParams().getUniverseUUID());
    }

    if (taskParams().serverType != ServerType.TSERVER
        && taskParams().serverType != ServerType.MASTER
        && taskParams().serverType != ServerType.CONTROLLER
        && taskParams().serverType != ServerType.YSQLSERVER) {
      throw new IllegalArgumentException(
          "Unexpected server type "
              + taskParams().serverType
              + " for universe "
              + taskParams().getUniverseUUID());
    }

    boolean isTserverTask =
        taskParams().serverType == ServerType.TSERVER
            || taskParams().serverType == ServerType.YSQLSERVER;
    if (isTserverTask && !node.isTserver) {
      log.warn(
          "Node {} is not yet updated to be a tserver but is being waited for", node.toString());
    }

    if (!isTserverTask && !node.isMaster) {
      log.warn(
          "Node {} is not yet updated to be a master but is being waited for", node.toString());
    }
  }
}
