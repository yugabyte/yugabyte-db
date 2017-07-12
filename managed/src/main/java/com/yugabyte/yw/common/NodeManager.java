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
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;

import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

@Singleton
public class NodeManager extends DevopsBase {
  private static final String YB_CLOUD_COMMAND_TYPE = "instance";

  @Inject
  ReleaseManager releaseManager;

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

    // TODO: [ENG-1242] we shouldn't be using our keypair, until we fix our VPC to support VPN
    if (userIntent != null && !userIntent.accessKeyCode.equalsIgnoreCase("yugabyte-default")) {
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

        // We only need to include keyPair name for setup server call and if this is aws.
        if (params instanceof AnsibleSetupServer.Params && params.cloud.equals(Common.CloudType.aws)) {
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

  private List<String> getDeviceArgs(NodeTaskParams params) {
    List<String> args = new ArrayList<>();
    if (params.deviceInfo.numVolumes != null && !params.getProvider().code.equals("onprem")) {
      args.add("--num_volumes");
      args.add(Integer.toString(params.deviceInfo.numVolumes));
    } else if (params.deviceInfo.mountPoints != null)  {
      args.add("--mount_points");
      args.add(params.deviceInfo.mountPoints);
    }
    if (params.deviceInfo.volumeSize != null) {
      args.add("--volume_size");
      args.add(Integer.toString(params.deviceInfo.volumeSize));
    }
    return args;
  }

  private String getThirdpartyPackagePath() {
    String packagePath = appConfig.getString("yb.thirdparty.packagePath");
    if (packagePath != null && !packagePath.isEmpty()) {
      File thirdpartyPackagePath = new File(packagePath);
      if (thirdpartyPackagePath.exists() && thirdpartyPackagePath.isDirectory()) {
        return packagePath;
      }
    }

    return null;
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

    String ybServerPackage = null;
    if (taskParam.ybSoftwareVersion != null) {
      ybServerPackage = releaseManager.getReleaseByVersion(taskParam.ybSoftwareVersion);
    }

    switch(taskParam.type) {
      case Everything:
        if (ybServerPackage == null) {
          throw new RuntimeException("Unable to fetch yugabyte release for version: " +
              taskParam.ybSoftwareVersion);
        }
        subcommand.add("--package");
        subcommand.add(ybServerPackage);
        Map<String, String> extra_gflags =
           Universe.get(taskParam.universeUUID).getUniverseDetails().userIntent.gflags;
        if (!extra_gflags.isEmpty()) {
          subcommand.add("--extra_gflags");
          subcommand.add(Json.stringify(Json.toJson(extra_gflags)));
        }
        break;
      case Software:
        {
          if (ybServerPackage == null) {
            throw new RuntimeException("Unable to fetch yugabyte release for version: " +
                taskParam.ybSoftwareVersion);
          }
          subcommand.add("--package");
          subcommand.add(ybServerPackage);
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
            throw new RuntimeException("Null processType property.");
          } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.MASTER.toString())) {
            subcommand.add("--tags");
            subcommand.add("master-gflags");
          } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.TSERVER.toString())) {
            subcommand.add("--tags");
            subcommand.add("tserver-gflags");
          } else {
            throw new RuntimeException("Invalid processType property: " + processType);
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
    List<String> commandArgs = new ArrayList<>();

    switch (type) {
      case Provision:
      {
        if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
        }
        AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params)nodeTaskParam;
        if (nodeTaskParam.cloud != Common.CloudType.onprem) {
          commandArgs.add("--instance_type");
          commandArgs.add(taskParam.instanceType);
          commandArgs.add("--cloud_subnet");
          commandArgs.add(taskParam.subnetId);
          commandArgs.add("--machine_image");
          commandArgs.add(taskParam.getRegion().ybImage);
          commandArgs.add("--assign_public_ip");
        }
        commandArgs.addAll(getAccessKeySpecificCommand(taskParam));
        if (nodeTaskParam.deviceInfo != null) {
          commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          DeviceInfo deviceInfo = nodeTaskParam.deviceInfo;
          if (deviceInfo.ebsType != null && nodeTaskParam.cloud == Common.CloudType.aws) {
            commandArgs.add("--volume_type");
            commandArgs.add(deviceInfo.ebsType.toString().toLowerCase());
            if (deviceInfo.ebsType.equals(DeviceInfo.EBSType.IO1) && deviceInfo.diskIops != null) {
              commandArgs.add("--disk_iops");
              commandArgs.add(Integer.toString(deviceInfo.diskIops));
            }
          }
        }

        String localPackagePath = getThirdpartyPackagePath();
        if (localPackagePath != null) {
          commandArgs.add("--local_package_path");
          commandArgs.add(localPackagePath);

          if (nodeTaskParam.cloud == Common.CloudType.onprem) {
            commandArgs.add("--air_gap");
          }
        }
        break;
      }
      case Configure:
      {
        if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
        }
        AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params)nodeTaskParam;
        commandArgs.addAll(getConfigureSubCommand(taskParam));
        commandArgs.addAll(getAccessKeySpecificCommand(taskParam));
        if (nodeTaskParam.deviceInfo != null) {
          commandArgs.addAll(getDeviceArgs(nodeTaskParam));
        }
        break;
      }
      case List:
      {
        commandArgs.add("--as_json");
        break;
      }
      case Destroy:
      {
        commandArgs.add("--instance_type");
        commandArgs.add(nodeTaskParam.instanceType);
        if (nodeTaskParam.deviceInfo != null) {
          commandArgs.addAll(getDeviceArgs(nodeTaskParam));
        }
        break;
      }
      case Control:
      {
        if (!(nodeTaskParam instanceof AnsibleClusterServerCtl.Params)) {
          throw new RuntimeException("NodeTaskParams is not AnsibleClusterServerCtl.Params");
        }
        AnsibleClusterServerCtl.Params taskParam = (AnsibleClusterServerCtl.Params)nodeTaskParam;
        commandArgs.add(taskParam.process);
        commandArgs.add(taskParam.command);
        commandArgs.addAll(getAccessKeySpecificCommand(taskParam));
        break;
      }
    }

    commandArgs.add(nodeTaskParam.nodeName);

    return execCommand(nodeTaskParam.getRegion().uuid, type.toString().toLowerCase(),
        commandArgs, getCloudArgs(nodeTaskParam));
  }
}
