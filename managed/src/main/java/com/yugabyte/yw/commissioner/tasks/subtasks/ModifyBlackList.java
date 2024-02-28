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
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.CommonNet.HostPortPB;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.YBClient;

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
    // The list of nodes to be added to the blacklist.
    public Collection<NodeDetails> addNodes;

    // The list of nodes to be removed from the blacklist.
    public Collection<NodeDetails> removeNodes;

    // When true, the tablet leaders on this node will move to another node, otherwise, move all
    // tablets on this node to other nodes
    public boolean isLeaderBlacklist;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", numAddNodes="
        + (CollectionUtils.isEmpty(taskParams().addNodes) ? 0 : taskParams().addNodes.size())
        + ", numRemoveNodes="
        + (CollectionUtils.isEmpty(taskParams().removeNodes) ? 0 : taskParams().removeNodes.size())
        + ", isLeaderBlacklist="
        + taskParams().isLeaderBlacklist
        + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    try {
      log.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);
      List<HostPortPB> addHosts = getHostPortPBs(universe, taskParams().addNodes);
      List<HostPortPB> removeHosts = getHostPortPBs(universe, taskParams().removeNodes);
      client = ybService.getClient(masterHostPorts, certificate);
      ModifyMasterClusterConfigBlacklist modifyBlackList =
          new ModifyMasterClusterConfigBlacklist(
              client, addHosts, removeHosts, taskParams().isLeaderBlacklist);
      modifyBlackList.doCall();
      universe.incrementVersion();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
  }

  private List<HostPortPB> getHostPortPBs(Universe universe, Collection<NodeDetails> nodes) {
    List<HostPortPB> hostPorts = null;
    if (CollectionUtils.isNotEmpty(nodes)) {
      hostPorts = new ArrayList<>(nodes.size());
      for (NodeDetails node : nodes) {
        String ip = Util.getNodeIp(universe, node);
        HostPortPB.Builder hpb = HostPortPB.newBuilder().setPort(node.tserverRpcPort).setHost(ip);
        hostPorts.add(hpb.build());
      }
    }
    return hostPorts;
  }
}
