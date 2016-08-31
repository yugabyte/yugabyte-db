// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.util.ArrayList;
import java.util.List;
import java.util.Collection;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common.HostPortPB;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.YBClient;
import org.yb.client.shaded.com.google.common.net.HostAndPort;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.ProtobufHelper;
import org.yb.master.Master;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.UniverseTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

// This class runs the task that helps modify the existing list of blacklisted servers maintained
// on the master leader.
public class ModifyBlackList extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(ModifyBlackList.class);

  // The YB client.
  public YBClientService ybService = null;

  // Parameters for placement info update task.
  public static class Params implements ITaskParams {
    // The universe against which this node's details should be saved.
    public UUID universeUUID;

    // When true, the collection of nodes below are added to the blacklist on the master leader,
    // else they are removed.
    public boolean isAdd;

    // The list of nodes being added or removed to this universes' configuration.
    public Collection<NodeDetails> nodes;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", isAdd=" +  taskParams().isAdd +
        ", numNodes=" +  taskParams().nodes.size() + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.get(taskParams().universeUUID);
    String masterHostPorts = universe.getMasterAddresses();
    try {
      LOG.info("Running {}: masterHostPorts={}.", getName(), masterHostPorts);
      List<HostPortPB> modifyHosts = new ArrayList<HostPortPB>();
      for (NodeDetails node : taskParams().nodes) {
        HostPortPB.Builder hpb =
            HostPortPB.newBuilder()
                      .setPort(node.tserverRpcPort)
                      .setHost(node.private_ip);
        modifyHosts.add(hpb.build());
      }
      ModifyMasterClusterConfigBlacklist modifyBlackList =
        new ModifyMasterClusterConfigBlacklist(ybService.getClient(masterHostPorts),
                                               modifyHosts,
                                               taskParams().isAdd);
      modifyBlackList.doCall();
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }
}
