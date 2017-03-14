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
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

@Singleton
public class NodeManager extends DevopsBase {
  private static final String YB_CLOUD_COMMAND_TYPE = "instance";

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  // Currently we need to define the enum such that the lower case value matches the action
  public enum NodeCommandType {
    Provision,
    Configure,
    Destroy,
    List,
    Control
  }
  public static final Logger LOG = LoggerFactory.getLogger(NodeManager.class);

  @Inject
  play.Configuration appConfig;

  private List<String> getCloudArgs(NodeTaskParams nodeTaskParam) {
    List<String> command = new ArrayList<String>();
    command.add("--zone");
    command.add(nodeTaskParam.getAZ().code);

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

  private List<String> getAccessKeySpecificCommand(NodeTaskParams params) {
    List<String> subCommand = new ArrayList<>();
    if (params.universeUUID == null) {
      throw new RuntimeException("NodeTaskParams missing Universe UUID.");
    }
    UniverseDefinitionTaskParams.UserIntent userIntent =
        Universe.get(params.universeUUID).getUniverseDetails().userIntent;

    if (userIntent != null && userIntent.accessKeyCode != null) {
      AccessKey accessKey = AccessKey.get(params.getProvider().uuid, userIntent.accessKeyCode);
      AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
      if (keyInfo.vaultFile != null) {
        subCommand.add("--vars_file");
        subCommand.add(keyInfo.vaultFile);
        subCommand.add("--vault_password_file");
        subCommand.add(keyInfo.vaultPasswordFile);
      }
      if (keyInfo.privateKey != null) {
        subCommand.add("--private_key_file");
        subCommand.add(keyInfo.privateKey);

        // We only need to include keyPair name for setup server call.
        if (params instanceof AnsibleSetupServer.Params) {
          subCommand.add("--key_pair_name");
          subCommand.add(userIntent.accessKeyCode);
          // Also we will add the security group name
          subCommand.add("--security_group");
          subCommand.add("yb-" + params.getRegion().code + "-sg");
        }
      }
    }

    return subCommand;
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

  /**
   * Adds the mount paths of the instance type as a comma-separated list to the devops command.
   *
   * @param providerCode
   * @param instanceTypeCode
   * @param command
   */
  private void addMountPaths(String providerCode, String instanceTypeCode, List<String> command) {
    InstanceType instanceType = InstanceType.get(providerCode, instanceTypeCode);
    if (instanceType == null) {
      throw new RuntimeException("No InstanceType exists for provider code " + providerCode +
                                 " and instance type code " + instanceTypeCode);
    }
    List<VolumeDetails> detailsList = instanceType.instanceTypeDetails.volumeDetailsList;
    String mountPoints = detailsList.stream()
                                    .map(volume -> volume.mountPath)
                                    .collect(Collectors.joining(","));
    if (!mountPoints.isEmpty()) {
      command.add("--mount_points");
      command.add(mountPoints);
    }
  }

  public ShellProcessHandler.ShellResponse nodeCommand(NodeCommandType type,
                                                       NodeTaskParams nodeTaskParam) throws RuntimeException {
    List<String> command = new ArrayList<>();
    command.add(type.toString().toLowerCase());

    switch (type) {
      case Provision:
      {
        if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
        }
        AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params)nodeTaskParam;
        if (nodeTaskParam.cloud != Common.CloudType.onprem) {
          command.add("--instance_type");
          command.add(taskParam.instanceType);
          command.add("--cloud_subnet");
          command.add(taskParam.subnetId);
          command.add("--machine_image");
          command.add(taskParam.getRegion().ybImage);
          command.add("--assign_public_ip");
        }
        command.addAll(getAccessKeySpecificCommand(taskParam));
        break;
      }
      case Configure:
      {
        if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
        }
        AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params)nodeTaskParam;
        command.addAll(getConfigureSubCommand(taskParam));
        command.addAll(getAccessKeySpecificCommand(taskParam));
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
        command.addAll(getAccessKeySpecificCommand(taskParam));
        break;
      }
    }
    if (!(nodeTaskParam.instanceType == null || nodeTaskParam.instanceType.isEmpty())) {
      addMountPaths(nodeTaskParam.getProvider().code, nodeTaskParam.instanceType, command);
    }

    command.add(nodeTaskParam.nodeName);

    return execCommand(nodeTaskParam.getRegion().uuid, command, getCloudArgs(nodeTaskParam));
  }
}
