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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public abstract class ServerSubTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ServerSubTaskBase.class);

  // The YB client.
  public YBClientService ybService = null;

  @Override
  protected ServerSubTaskParams taskParams() {
    return (ServerSubTaskParams)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", " + taskParams().nodeName +
           ", type=" + taskParams().serverType + ")";
  }

  public String getMasterAddresses() {
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    return masterAddresses;
  }

  public HostAndPort getHostPort() {
    NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);
    HostAndPort hp = HostAndPort.fromParts(
        node.cloudInfo.private_ip,
        taskParams().serverType == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
    return hp;
  }

  public YBClient getClient() {
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    return ybService.getClient(masterAddresses, certificate);
  }

  public void closeClient(YBClient client) {
    ybService.closeClient(client, getMasterAddresses());
  }

  public void checkParams() {
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    LOG.info("Running {} on masterAddress = {}.", getName(), masterAddresses);

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      throw new IllegalArgumentException("Invalid master addresses " + masterAddresses + " for " +
          taskParams().universeUUID);
    }

    NodeDetails node = universe.getNode(taskParams().nodeName);

    if (node == null) {
      throw new IllegalArgumentException("Node " + taskParams().nodeName + " not found in " +
                                         "universe " + taskParams().universeUUID);
    }

    if (taskParams().serverType != ServerType.TSERVER &&
        taskParams().serverType != ServerType.MASTER) {
      throw new IllegalArgumentException("Unexpected server type " + taskParams().serverType +
          " for universe " + taskParams().universeUUID);
    }

    boolean isTserverTask = taskParams().serverType == ServerType.TSERVER;
    if (isTserverTask && !node.isTserver) {
      throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is " +
                                         "not for a node running tserver : " + node.toString());
    }

    if (!isTserverTask && !node.isMaster) {
      throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is " +
                                         "not for a node running master : " + node.toString());
    }
  }

}
