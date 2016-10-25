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
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.models.Universe;

import play.libs.Json;

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

  public static final String YBCLOUD_SCRIPT = "/bin/ybcloud.sh";

  @Inject
  play.Configuration appConfig;

  private String cloudBaseCommand(NodeTaskParams nodeTaskParam) {
    String command = appConfig.getString("yb.devops.home") + YBCLOUD_SCRIPT + " " + nodeTaskParam.cloud;
    command += " --zone " + nodeTaskParam.getAZ().code;
    command += " --region " + nodeTaskParam.getRegion().code;
    return command;
  }

  private String getConfigureSubCommand(AnsibleConfigureServers.Params taskParam) {
    String subcommand = "";

    String masterAddresses = Universe.get(taskParam.universeUUID).getMasterAddresses();
    subcommand += " --master_addresses_for_tserver " + masterAddresses;

    if (!taskParam.isMasterInShellMode) {
      subcommand += " --master_addresses_for_master " + masterAddresses;
    }

    switch(taskParam.type) {
      case Everything:
        subcommand += " --package " + taskParam.ybServerPackage;
        break;
      case Software:
        {
          subcommand += " --package " + taskParam.ybServerPackage;
          String taskSubType = taskParam.getProperty("taskSubType");
          if (taskSubType == null) {
            throw new RuntimeException("Invalid taskSubType property: " + taskSubType);
          } else if (taskSubType.equals(UpgradeUniverse.UpgradeTaskSubType.Download.toString())) {
            subcommand += " --tags download-software";
          } else if (taskSubType.equals(UpgradeUniverse.UpgradeTaskSubType.Install.toString())) {
            subcommand += " --tags install-software";
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
            subcommand += " --tags master-gflags";
          } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.TSERVER)) {
            subcommand += " --tags tserver-gflags";
          }
          subcommand += " --force_gflags --gflags " + Json.stringify(Json.toJson(taskParam.gflags));
        }
        break;
    }
    return subcommand;
  }

  public String nodeCommand(NodeCommandType type, NodeTaskParams nodeTaskParam) throws RuntimeException {
    String command = cloudBaseCommand(nodeTaskParam);

    command += " instance " + type.toString().toLowerCase();

    switch (type) {
      case Provision:
      {
        if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
        }
        AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params)nodeTaskParam;
        command += " --cloud_subnet " + taskParam.subnetId +
          " --machine_image " + taskParam.getRegion().ybImage +
          " --instance_type " + taskParam.instanceType +
          " --assign_public_ip";
        break;
      }
      case Configure:
      {
        if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
        }
        AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params)nodeTaskParam;
        command += getConfigureSubCommand(taskParam);
        break;
      }
      case List:
      {
        if (!(nodeTaskParam instanceof AnsibleUpdateNodeInfo.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleUpdateNodeInfo.Params");
        }
        command += " --as_json";
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
        command += " " + taskParam.process + " " + taskParam.command;
        break;
      }
    }

    command += " " + nodeTaskParam.nodeName;
    return command;
  }
}
