// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.ArrayList;
import java.util.List;

@Singleton
public class DevOpsHelper {
  // Currently we need to define the enum such that the lower case value matches the action
  public enum NodeCommandType {
    Provision,
    Configure,
    Destroy,
    List,
    Control
  }
  public static final Logger LOG = LoggerFactory.getLogger(DevOpsHelper.class);

  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";

  @Inject
  play.Configuration appConfig;

  @Inject
  ShellProcessHandler shellProcessHandler;

  private List<String> cloudBaseCommand(NodeTaskParams nodeTaskParam) {
    List<String> command = new ArrayList<String>();
    command.add(YBCLOUD_SCRIPT);
    command.add(nodeTaskParam.cloud.toString());
    command.add("--zone");
    command.add(nodeTaskParam.getAZ().code);
    command.add("--region");
    command.add(nodeTaskParam.getRegion().code);

    // Right now for docker we grab the network from application conf.
    if (nodeTaskParam.cloud == Common.CloudType.docker) {
      String networkName = appConfig.getString("yb.docker.network");
      if (networkName == null) {
        throw new RuntimeException("yb.docker.network is not set in application.conf");
      }
      command.add("--network");
      command.add(networkName);
    }

    if (nodeTaskParam.cloud == Common.CloudType.onprem) {
      NodeInstance node = NodeInstance.getByName(nodeTaskParam.nodeName);
      command.add("--node_metadata");
      command.add(node.getDetailsJson());
    }
    return command;
  }

  private List<String> getConfigureSubCommand(AnsibleConfigureServers.Params taskParam) {
    List<String> subcommand = new ArrayList<String>();

    String masterAddresses = Universe.get(taskParam.universeUUID).getMasterAddresses(false);
    subcommand.add("--master_addresses_for_tserver");
    subcommand.add(masterAddresses);

    if (!taskParam.isMasterInShellMode) {
      subcommand.add("--master_addresses_for_master");
      subcommand.add(masterAddresses);
    }

    switch(taskParam.type) {
      case Everything:
        subcommand.add("--package");
        subcommand.add(taskParam.ybServerPackage);
        break;
      case Software:
        {
          subcommand.add("--package");
          subcommand.add(taskParam.ybServerPackage);
          String taskSubType = taskParam.getProperty("taskSubType");
          if (taskSubType == null) {
            throw new RuntimeException("Invalid taskSubType property: " + taskSubType);
          } else if (taskSubType.equals(UpgradeUniverse.UpgradeTaskSubType.Download.toString())) {
            subcommand.add("--tags");
            subcommand.add("download-software");
          } else if (taskSubType.equals(UpgradeUniverse.UpgradeTaskSubType.Install.toString())) {
            subcommand.add("--tags");
            subcommand.add("install-software");
          }
        }
        break;
      case GFlags:
        {
          if (taskParam.gflags == null || taskParam.gflags.isEmpty() ) {
            throw new RuntimeException("Empty GFlags data provided");
          }

          String processType = taskParam.getProperty("processType");

          if (processType == null) {
            throw new RuntimeException("Invalid processType property: " + processType);
          } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.MASTER)) {
            subcommand.add("--tags");
            subcommand.add("master-gflags");
          } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.TSERVER)) {
            subcommand.add("--tags");
            subcommand.add("tserver-gflags");
          }
          subcommand.add("--replace_gflags");
          subcommand.add("--gflags");
          subcommand.add(Json.stringify(Json.toJson(taskParam.gflags)));
        }
        break;
    }
    return subcommand;
  }

  public ShellProcessHandler.ShellResponse nodeCommand(NodeCommandType type,
                                                       NodeTaskParams nodeTaskParam) throws RuntimeException {
    List<String> command = cloudBaseCommand(nodeTaskParam);
    command.add("instance");
    command.add(type.toString().toLowerCase());

    switch (type) {
      case Provision:
      {
        if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
        }
        AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params)nodeTaskParam;
        command.add("--instance_type");
        command.add(taskParam.instanceType);
        if (nodeTaskParam.cloud != Common.CloudType.onprem) {
          command.add("--cloud_subnet");
          command.add(taskParam.subnetId);
          command.add("--machine_image");
          command.add(taskParam.getRegion().ybImage);
          command.add("--assign_public_ip");
        }
        break;
      }
      case Configure:
      {
        if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
        }
        AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params)nodeTaskParam;
        command.addAll(getConfigureSubCommand(taskParam));
        break;
      }
      case List:
      {
        if (!(nodeTaskParam instanceof AnsibleUpdateNodeInfo.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleUpdateNodeInfo.Params");
        }
        command.add("--as_json");
        break;
      }
      case Destroy:
      {
        if (!(nodeTaskParam instanceof AnsibleDestroyServer.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleDestroyServer.Params");
        }
        break;
      }
      case Control:
      {
        if (!(nodeTaskParam instanceof AnsibleClusterServerCtl.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleClusterServerCtl.Params");
        }
        AnsibleClusterServerCtl.Params taskParam = (AnsibleClusterServerCtl.Params)nodeTaskParam;
        command.add(taskParam.process);
        command.add(taskParam.command);
        break;
      }
    }

    command.add(nodeTaskParam.nodeName);

    Provider provider = nodeTaskParam.getRegion().provider;
    LOG.info("Command to run: [" + String.join(" ", command) + "]");
    return shellProcessHandler.run(command, provider.getConfig());
  }
}
