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
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpgradeServer;
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

  private String cloudBaseCommand(Common.CloudType cloud) {
    return appConfig.getString("yb.devops.home") + YBCLOUD_SCRIPT + " " + cloud;
  }

  private String upgradeSpecificSubCommand(AnsibleUpgradeServer.Params taskParam) {
    String subcommand = "";
    switch(taskParam.taskType) {
      case Software:
        {
          subcommand += " --package " + taskParam.ybServerPkg;
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
    String command = cloudBaseCommand(nodeTaskParam.cloud);

    if (nodeTaskParam.cloud == Common.CloudType.aws) {
      command += " --region " + nodeTaskParam.getRegion().code;
    }

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
        String masterAddresses = Universe.get(nodeTaskParam.universeUUID).getMasterAddresses();
        command += " --master_addresses_for_tserver " + masterAddresses;

        if (nodeTaskParam instanceof AnsibleConfigureServers.Params) {
          AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params)nodeTaskParam;

          if (!taskParam.isMasterInShellMode) {
            command += " --master_addresses_for_master " + masterAddresses;
          }

          command += " --package " + taskParam.ybServerPkg;
        } else if (nodeTaskParam instanceof AnsibleUpgradeServer.Params) {
          AnsibleUpgradeServer.Params taskParam = (AnsibleUpgradeServer.Params)nodeTaskParam;
          command += upgradeSpecificSubCommand(taskParam);
        } else {
          throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params or " +
                                       "AnsibleUpgradeServer.Params");
        }

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
