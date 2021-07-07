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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;
import org.yb.Common.HostPortPB;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.YBClient;

import javax.inject.Inject;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

// This class runs the task that helps modify the existing list of blacklisted servers maintained
// on the master leader.
@Slf4j
public class ModifyBlackList extends UniverseTaskBase {

  @Inject
  protected ModifyBlackList(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for placement info update task.
  public static class Params extends UniverseTaskParams {
    // When true, the collection of nodes below are added to the blacklist on the master leader,
    // else they are removed.
    public boolean isAdd;

    // The list of nodes being added or removed to this universes' configuration.
    public Collection<NodeDetails> nodes;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().universeUUID
        + ", isAdd="
        + taskParams().isAdd
        + ", numNodes="
        + taskParams().nodes.size()
        + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);
      List<HostPortPB> modifyHosts = new ArrayList<HostPortPB>();
      for (NodeDetails node : taskParams().nodes) {
        String ip = node.cloudInfo.private_ip;
        if (ip == null) {
          NodeDetails onDiskNode = universe.getNode(node.nodeName);
          ip = onDiskNode.cloudInfo.private_ip;
        }
        HostPortPB.Builder hpb = HostPortPB.newBuilder().setPort(node.tserverRpcPort).setHost(ip);
        modifyHosts.add(hpb.build());
      }
      client = ybService.getClient(masterHostPorts, certificate);
      ModifyMasterClusterConfigBlacklist modifyBlackList =
          new ModifyMasterClusterConfigBlacklist(client, modifyHosts, taskParams().isAdd);
      modifyBlackList.doCall();
      universe.incrementVersion();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
  }
}
