/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.PauseServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;

@Singleton
public class NodeManager extends DevopsBase {
  static final String BOOT_SCRIPT_PATH = "yb.universe_boot_script";
  private static final String YB_CLOUD_COMMAND_TYPE = "instance";
  private static final List<String> VALID_CONFIGURE_PROCESS_TYPES =
      ImmutableList.of(ServerType.MASTER.name(), ServerType.TSERVER.name());

  @Inject ReleaseManager releaseManager;

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  // Currently we need to define the enum such that the lower case value matches the action
  public enum NodeCommandType {
    Provision,
    Configure,
    CronCheck,
    Destroy,
    List,
    Control,
    Precheck,
    Tags,
    InitYSQL,
    Disk_Update,
    Pause,
    Resume,
    Create_Root_Volumes,
    Replace_Root_Volume
  }

  public static final Logger LOG = LoggerFactory.getLogger(NodeManager.class);

  @Inject play.Configuration appConfig;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  private UserIntent getUserIntentFromParams(NodeTaskParams nodeTaskParam) {
    Universe universe = Universe.getOrBadRequest(nodeTaskParam.universeUUID);
    NodeDetails nodeDetails = universe.getNode(nodeTaskParam.nodeName);
    if (nodeDetails == null) {
      nodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
      LOG.info("Node {} not found, so using {}.", nodeTaskParam.nodeName, nodeDetails.nodeName);
    }
    return universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;
  }

  private List<String> getCloudArgs(NodeTaskParams nodeTaskParam) {
    List<String> command = new ArrayList<String>();
    command.add("--zone");
    command.add(nodeTaskParam.getAZ().code);
    UserIntent userIntent = getUserIntentFromParams(nodeTaskParam);

    // Right now for docker we grab the network from application conf.
    if (userIntent.providerType.equals(Common.CloudType.docker)) {
      String networkName = appConfig.getString("yb.docker.network");
      if (networkName == null) {
        throw new RuntimeException("yb.docker.network is not set in application.conf");
      }
      command.add("--network");
      command.add(networkName);
    }

    if (userIntent.providerType.equals(Common.CloudType.onprem)) {
      NodeInstance node = NodeInstance.getByName(nodeTaskParam.nodeName);
      command.add("--node_metadata");
      command.add(node.getDetailsJson());
    }
    return command;
  }

  private List<String> getAccessKeySpecificCommand(NodeTaskParams params, NodeCommandType type) {
    List<String> subCommand = new ArrayList<>();
    if (params.universeUUID == null) {
      throw new RuntimeException("NodeTaskParams missing Universe UUID.");
    }
    UserIntent userIntent = getUserIntentFromParams(params);

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
        if (params instanceof AnsibleSetupServer.Params
            && userIntent.providerType.equals(Common.CloudType.aws)) {
          subCommand.add("--key_pair_name");
          subCommand.add(userIntent.accessKeyCode);
          // Also we will add the security group information.
          Region r = params.getRegion();
          String customSecurityGroupId = r.getSecurityGroupId();
          if (customSecurityGroupId != null) {
            subCommand.add("--security_group_id");
            subCommand.add(customSecurityGroupId);
          }
        }
      }
      if (params instanceof AnsibleSetupServer.Params
          && userIntent.providerType.equals(Common.CloudType.azu)) {
        Region r = params.getRegion();
        String customSecurityGroupId = r.getSecurityGroupId();
        if (customSecurityGroupId != null) {
          subCommand.add("--security_group_id");
          subCommand.add(customSecurityGroupId);
        }
      }

      if (params instanceof AnsibleDestroyServer.Params
          && userIntent.providerType.equals(Common.CloudType.onprem)) {
        subCommand.add("--install_node_exporter");
      }

      subCommand.add("--custom_ssh_port");
      subCommand.add(keyInfo.sshPort.toString());

      if ((type == NodeCommandType.Provision || type == NodeCommandType.Destroy)
          && keyInfo.sshUser != null) {
        subCommand.add("--ssh_user");
        subCommand.add(keyInfo.sshUser);
      }

      if (type == NodeCommandType.Precheck) {
        subCommand.add("--precheck_type");
        if (keyInfo.skipProvisioning) {
          subCommand.add("configure");
          subCommand.add("--ssh_user");
          subCommand.add("yugabyte");
        } else {
          subCommand.add("provision");
          if (keyInfo.sshUser != null) {
            subCommand.add("--ssh_user");
            subCommand.add(keyInfo.sshUser);
          }
        }

        if (keyInfo.airGapInstall) {
          subCommand.add("--air_gap");
        }
        if (keyInfo.installNodeExporter) {
          subCommand.add("--install_node_exporter");
        }
      }

      if (params instanceof AnsibleSetupServer.Params) {
        if (keyInfo.airGapInstall) {
          subCommand.add("--air_gap");
        }

        if (keyInfo.installNodeExporter) {
          subCommand.add("--install_node_exporter");
          subCommand.add("--node_exporter_port");
          subCommand.add(Integer.toString(keyInfo.nodeExporterPort));
          subCommand.add("--node_exporter_user");
          subCommand.add(keyInfo.nodeExporterUser);
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
    } else if (params.deviceInfo.mountPoints != null) {
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
    UserIntent userIntent = getUserIntentFromParams(taskParam);
    List<String> subcommand = new ArrayList<String>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    String masterAddresses = universe.getMasterAddresses(false);
    subcommand.add("--master_addresses_for_tserver");
    subcommand.add(masterAddresses);

    if (masterAddresses == null || masterAddresses.isEmpty()) {
      LOG.warn("No valid masters found during configure for {}.", taskParam.universeUUID);
    }

    if (!taskParam.isMasterInShellMode) {
      subcommand.add("--master_addresses_for_master");
      subcommand.add(masterAddresses);
    }

    String ybServerPackage = null;
    if (taskParam.ybSoftwareVersion != null) {
      ReleaseManager.ReleaseMetadata releaseMetadata =
          releaseManager.getReleaseByVersion(taskParam.ybSoftwareVersion);
      if (releaseMetadata != null) {
        ybServerPackage = releaseMetadata.filePath;
      }
    }

    if (!taskParam.itestS3PackagePath.isEmpty()
        && userIntent.providerType.equals(Common.CloudType.aws)) {
      subcommand.add("--itest_s3_package_path");
      subcommand.add(taskParam.itestS3PackagePath);
    }

    NodeDetails node = universe.getNode(taskParam.nodeName);

    // Pass in communication ports
    subcommand.add("--master_http_port");
    subcommand.add(Integer.toString(node.masterHttpPort));
    subcommand.add("--master_rpc_port");
    subcommand.add(Integer.toString(node.masterRpcPort));
    subcommand.add("--tserver_http_port");
    subcommand.add(Integer.toString(node.tserverHttpPort));
    subcommand.add("--tserver_rpc_port");
    subcommand.add(Integer.toString(node.tserverRpcPort));
    subcommand.add("--cql_proxy_rpc_port");
    subcommand.add(Integer.toString(node.yqlServerRpcPort));
    subcommand.add("--redis_proxy_rpc_port");
    subcommand.add(Integer.toString(node.redisServerRpcPort));

    switch (taskParam.type) {
      case Everything:
        boolean useHostname =
            universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname;
        if (ybServerPackage == null) {
          throw new RuntimeException(
              "Unable to fetch yugabyte release for version: " + taskParam.ybSoftwareVersion);
        }
        subcommand.add("--package");
        subcommand.add(ybServerPackage);
        Map<String, String> extra_gflags = new HashMap<>();
        extra_gflags.put("undefok", "enable_ysql");
        if (taskParam.isMaster) {
          extra_gflags.put("cluster_uuid", String.valueOf(taskParam.universeUUID));
          extra_gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
        }
        extra_gflags.put("placement_uuid", String.valueOf(taskParam.placementUuid));
        // Add in the nodeName during configure.
        extra_gflags.put("metric_node_name", taskParam.nodeName);
        // TODO: add a shared path to massage flags across different flavors of configure.
        String pgsqlProxyBindAddress = node.cloudInfo.private_ip;

        if (useHostname) {
          subcommand.add("--server_broadcast_addresses");
          subcommand.add(node.cloudInfo.private_ip);
          pgsqlProxyBindAddress = "0.0.0.0";
        }

        if (taskParam.enableYSQL) {
          extra_gflags.put("enable_ysql", "true");
          extra_gflags.put(
              "pgsql_proxy_bind_address",
              String.format("%s:%s", pgsqlProxyBindAddress, node.ysqlServerRpcPort));
        } else {
          extra_gflags.put("enable_ysql", "false");
        }

        if (taskParam.getCurrentClusterType() == UniverseDefinitionTaskParams.ClusterType.PRIMARY
            && taskParam.setTxnTableWaitCountFlag) {
          extra_gflags.put(
              "txn_table_wait_min_ts_count",
              Integer.toString(
                  universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes));
        }

        if ((taskParam.enableNodeToNodeEncrypt || taskParam.enableClientToNodeEncrypt)) {
          if (taskParam.enableNodeToNodeEncrypt) {
            extra_gflags.put("use_node_to_node_encryption", "true");
          }
          if (taskParam.enableClientToNodeEncrypt) {
            extra_gflags.put("use_client_to_server_encryption", "true");
          }
          extra_gflags.put(
              "allow_insecure_connections", taskParam.allowInsecure ? "true" : "false");
          String yb_home_dir = taskParam.getProvider().getYbHome();

          extra_gflags.put("cert_node_filename", node.cloudInfo.private_ip);

          if (taskParam.rootAndClientRootCASame) {
            extra_gflags.put("certs_dir", yb_home_dir + "/yugabyte-tls-config");
            subcommand.add("--certs_node_dir");
            subcommand.add(yb_home_dir + "/yugabyte-tls-config");

            CertificateInfo rootCert = CertificateInfo.get(taskParam.rootCA);
            if (rootCert == null) {
              throw new RuntimeException("No valid rootCA found for " + taskParam.universeUUID);
            }

            switch (rootCert.certType) {
              case SelfSigned:
                {
                  subcommand.add("--rootCA_cert");
                  subcommand.add(rootCert.certificate);
                  subcommand.add("--rootCA_key");
                  subcommand.add(rootCert.privateKey);
                  break;
                }
              case CustomCertHostPath:
                {
                  CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertInfo();
                  subcommand.add("--use_custom_certs");
                  subcommand.add("--root_cert_path");
                  subcommand.add(customCertInfo.rootCertPath);
                  subcommand.add("--node_cert_path");
                  subcommand.add(customCertInfo.nodeCertPath);
                  subcommand.add("--node_key_path");
                  subcommand.add(customCertInfo.nodeKeyPath);
                  if (customCertInfo.clientCertPath != null
                      && !customCertInfo.clientCertPath.isEmpty()
                      && customCertInfo.clientKeyPath != null
                      && !customCertInfo.clientKeyPath.isEmpty()) {
                    // These client certs are used for node to postgres communication
                    // These are seprate from clientRoot certs which are used for server to client
                    // comm
                    // These are not required anymore as this is not mandatory now and can be
                    // removed
                    // The code is still here to mantain backward compatibility
                    subcommand.add("--client_cert_path");
                    subcommand.add(customCertInfo.clientCertPath);
                    subcommand.add("--client_key_path");
                    subcommand.add(customCertInfo.clientKeyPath);
                  }
                  break;
                }
              case CustomServerCert:
                {
                  throw new RuntimeException("rootCA cannot be of type CustomServerCert.");
                }
            }
          } else {
            if (taskParam.enableNodeToNodeEncrypt) {
              extra_gflags.put("certs_dir", yb_home_dir + "/yugabyte-tls-config");
              subcommand.add("--certs_node_dir");
              subcommand.add(yb_home_dir + "/yugabyte-tls-config");

              CertificateInfo rootCert = CertificateInfo.get(taskParam.rootCA);
              if (rootCert == null) {
                throw new RuntimeException("No valid rootCA found for " + taskParam.universeUUID);
              }

              switch (rootCert.certType) {
                case SelfSigned:
                  {
                    subcommand.add("--rootCA_cert");
                    subcommand.add(rootCert.certificate);
                    subcommand.add("--rootCA_key");
                    subcommand.add(rootCert.privateKey);
                    break;
                  }
                case CustomCertHostPath:
                  {
                    CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertInfo();
                    subcommand.add("--use_custom_certs");
                    subcommand.add("--root_cert_path");
                    subcommand.add(customCertInfo.rootCertPath);
                    subcommand.add("--node_cert_path");
                    subcommand.add(customCertInfo.nodeCertPath);
                    subcommand.add("--node_key_path");
                    subcommand.add(customCertInfo.nodeKeyPath);
                    break;
                  }
                case CustomServerCert:
                  {
                    throw new RuntimeException("rootCA cannot be of type CustomServerCert.");
                  }
              }
            }
            if (taskParam.enableClientToNodeEncrypt) {
              extra_gflags.put("certs_for_client_dir", yb_home_dir + "/yugabyte-client-tls-config");
              subcommand.add("--certs_client_dir");
              subcommand.add(yb_home_dir + "/yugabyte-client-tls-config");

              CertificateInfo clientRootCert = CertificateInfo.get(taskParam.clientRootCA);
              if (clientRootCert == null) {
                throw new RuntimeException(
                    "No valid clientRootCA found for " + taskParam.universeUUID);
              }

              switch (clientRootCert.certType) {
                case SelfSigned:
                  {
                    subcommand.add("--clientRootCA_cert");
                    subcommand.add(clientRootCert.certificate);
                    subcommand.add("--clientRootCA_key");
                    subcommand.add(clientRootCert.privateKey);
                    break;
                  }
                case CustomCertHostPath:
                  {
                    CertificateParams.CustomCertInfo customCertInfo =
                        clientRootCert.getCustomCertInfo();
                    subcommand.add("--use_custom_client_certs");
                    subcommand.add("--client_root_cert_path");
                    subcommand.add(customCertInfo.rootCertPath);
                    subcommand.add("--client_node_cert_path");
                    subcommand.add(customCertInfo.nodeCertPath);
                    subcommand.add("--client_node_key_path");
                    subcommand.add(customCertInfo.nodeKeyPath);
                    break;
                  }
                case CustomServerCert:
                  {
                    CertificateInfo.CustomServerCertInfo customServerCertInfo =
                        clientRootCert.getCustomServerCertInfo();
                    subcommand.add("--use_custom_server_certs");
                    subcommand.add("--server_root_cert");
                    subcommand.add(clientRootCert.certificate);
                    subcommand.add("--server_node_cert");
                    subcommand.add(customServerCertInfo.serverCert);
                    subcommand.add("--server_node_key");
                    subcommand.add(customServerCertInfo.serverKey);
                  }
              }
            }
          }
        }
        if (taskParam.callhomeLevel != null) {
          extra_gflags.put(
              "callhome_collection_level", taskParam.callhomeLevel.toString().toLowerCase());
          if (taskParam.callhomeLevel.toString().equals("NONE")) {
            extra_gflags.put("callhome_enabled", "false");
          }
        }
        subcommand.add("--extra_gflags");
        subcommand.add(Json.stringify(Json.toJson(extra_gflags)));
        break;
      case Software:
        {
          if (ybServerPackage == null) {
            throw new RuntimeException(
                "Unable to fetch yugabyte release for version: " + taskParam.ybSoftwareVersion);
          }
          subcommand.add("--package");
          subcommand.add(ybServerPackage);
          String processType = taskParam.getProperty("processType");
          if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
            throw new RuntimeException("Invalid processType: " + processType);
          } else {
            subcommand.add("--yb_process_type");
            subcommand.add(processType.toLowerCase());
          }
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
          Map<String, String> gflags = new HashMap<>();
          gflags.put("placement_uuid", String.valueOf(taskParam.placementUuid));
          subcommand.add("--extra_gflags");
          subcommand.add(Json.stringify(Json.toJson(gflags)));
        }
        break;
      case GFlags:
        {
          if (!taskParam.updateMasterAddrsOnly
              && (taskParam.gflags == null || taskParam.gflags.isEmpty())
              && (taskParam.gflagsToRemove == null || taskParam.gflagsToRemove.isEmpty())) {
            throw new RuntimeException(
                "GFlags data provided for "
                    + taskParam.nodeName
                    + "'s "
                    + taskParam.getProperty("processType")
                    + " process"
                    + " have no changes from existing flags.");
          }

          String processType = taskParam.getProperty("processType");
          if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
            throw new RuntimeException("Invalid processType: " + processType);
          } else {
            subcommand.add("--yb_process_type");
            subcommand.add(processType.toLowerCase());
          }
          subcommand.add("--replace_gflags");

          // Add in the nodeName during configure.
          Map<String, String> gflags = new HashMap<>(taskParam.gflags);

          if (taskParam.updateMasterAddrsOnly) {
            if (taskParam.isMasterInShellMode) {
              masterAddresses = "";
            }
            if (processType.equals(ServerType.MASTER.name())) {
              gflags.put("master_addresses", masterAddresses);
            } else {
              gflags.put("tserver_master_addrs", masterAddresses);
            }

          } else {
            gflags.put("placement_uuid", String.valueOf(taskParam.placementUuid));
            gflags.put("metric_node_name", taskParam.nodeName);
          }
          subcommand.add("--gflags");
          subcommand.add(Json.stringify(Json.toJson(gflags)));

          if (taskParam.gflagsToRemove != null && !taskParam.gflagsToRemove.isEmpty()) {
            subcommand.add("--gflags_to_remove");
            subcommand.add(Json.stringify(Json.toJson(taskParam.gflagsToRemove)));
          }

          subcommand.add("--tags");
          subcommand.add("override_gflags");
        }
        break;
      case Certs:
        {
          CertificateInfo cert = CertificateInfo.get(taskParam.rootCA);
          if (cert == null) {
            throw new RuntimeException("Certificate is null: " + taskParam.rootCA);
          }
          if (cert.certType == CertificateInfo.Type.SelfSigned) {
            throw new RuntimeException("Self signed certs cannot be rotated.");
          }
          String processType = taskParam.getProperty("processType");
          if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
            throw new RuntimeException("Invalid processType: " + processType);
          } else {
            subcommand.add("--yb_process_type");
            subcommand.add(processType.toLowerCase());
          }
          CertificateParams.CustomCertInfo customCertInfo = cert.getCustomCertInfo();
          subcommand.add("--use_custom_certs");
          subcommand.add("--rotating_certs");
          subcommand.add("--root_cert_path");
          subcommand.add(customCertInfo.rootCertPath);
          subcommand.add("--node_cert_path");
          subcommand.add(customCertInfo.nodeCertPath);
          subcommand.add("--node_key_path");
          subcommand.add(customCertInfo.nodeKeyPath);
          if (customCertInfo.clientCertPath != null
              && !customCertInfo.clientCertPath.isEmpty()
              && customCertInfo.clientKeyPath != null
              && !customCertInfo.clientKeyPath.isEmpty()) {
            subcommand.add("--client_cert_path");
            subcommand.add(customCertInfo.clientCertPath);
            subcommand.add("--client_key_path");
            subcommand.add(customCertInfo.clientKeyPath);
          }
        }
        break;
      case ToggleTls:
        String processType = taskParam.getProperty("processType");
        String subType = taskParam.getProperty("taskSubType");

        if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
          throw new RuntimeException("Invalid processType: " + processType);
        } else {
          subcommand.add("--yb_process_type");
          subcommand.add(processType.toLowerCase());
        }

        String nodeToNodeString = String.valueOf(taskParam.enableNodeToNodeEncrypt);
        String clientToNodeString = String.valueOf(taskParam.enableClientToNodeEncrypt);
        String allowInsecureString = String.valueOf(taskParam.allowInsecure);

        String certsNodeDir =
            Provider.getOrBadRequest(
                        UUID.fromString(
                            universe.getUniverseDetails().getPrimaryCluster().userIntent.provider))
                    .getYbHome()
                + "/yugabyte-tls-config";

        if (UpgradeUniverse.UpgradeTaskSubType.CopyCerts.name().equals(subType)) {
          if (taskParam.enableNodeToNodeEncrypt || taskParam.enableClientToNodeEncrypt) {
            CertificateInfo cert = CertificateInfo.get(taskParam.rootCA);
            if (cert == null) {
              throw new RuntimeException("No valid rootCA found for " + taskParam.universeUUID);
            }

            subcommand.add("--adding_certs");
            subcommand.add("--certs_node_dir");
            subcommand.add(certsNodeDir);

            if (cert.certType == CertificateInfo.Type.SelfSigned) {
              subcommand.add("--rootCA_cert");
              subcommand.add(cert.certificate);
              subcommand.add("--rootCA_key");
              subcommand.add(cert.privateKey);
              if (taskParam.enableClientToNodeEncrypt) {
                subcommand.add("--client_cert");
                subcommand.add(CertificateHelper.getClientCertFile(taskParam.rootCA));
                subcommand.add("--client_key");
                subcommand.add(CertificateHelper.getClientKeyFile(taskParam.rootCA));
              }
            } else {
              CertificateParams.CustomCertInfo customCertInfo = cert.getCustomCertInfo();
              subcommand.add("--use_custom_certs");
              subcommand.add("--root_cert_path");
              subcommand.add(customCertInfo.rootCertPath);
              subcommand.add("--node_cert_path");
              subcommand.add(customCertInfo.nodeCertPath);
              subcommand.add("--node_key_path");
              subcommand.add(customCertInfo.nodeKeyPath);
              if (customCertInfo.clientCertPath != null
                  && !customCertInfo.clientCertPath.isEmpty()
                  && customCertInfo.clientKeyPath != null
                  && !customCertInfo.clientKeyPath.isEmpty()) {
                subcommand.add("--client_cert_path");
                subcommand.add(customCertInfo.clientCertPath);
                subcommand.add("--client_key_path");
                subcommand.add(customCertInfo.clientKeyPath);
              }
            }
          } else {
            throw new RuntimeException("No changes needed for both root cert and client cert.");
          }
        } else if (UpgradeUniverse.UpgradeTaskSubType.Round1GFlagsUpdate.name().equals(subType)) {
          Map<String, String> gflags = new HashMap<>();
          if (taskParam.nodeToNodeChange > 0) {
            gflags.put("use_node_to_node_encryption", nodeToNodeString);
            gflags.put("use_client_to_server_encryption", clientToNodeString);
            gflags.put("allow_insecure_connections", "true");
            gflags.put("certs_dir", certsNodeDir);
          } else if (taskParam.nodeToNodeChange < 0) {
            gflags.put("allow_insecure_connections", "true");
          } else {
            gflags.put("use_node_to_node_encryption", nodeToNodeString);
            gflags.put("use_client_to_server_encryption", clientToNodeString);
            gflags.put("allow_insecure_connections", allowInsecureString);
            gflags.put("certs_dir", certsNodeDir);
          }

          subcommand.add("--replace_gflags");
          subcommand.add("--gflags");
          subcommand.add(Json.stringify(Json.toJson(gflags)));
        } else if (UpgradeUniverse.UpgradeTaskSubType.Round2GFlagsUpdate.name().equals(subType)) {
          Map<String, String> gflags = new HashMap<>();
          if (taskParam.nodeToNodeChange > 0) {
            gflags.put("allow_insecure_connections", allowInsecureString);
          } else if (taskParam.nodeToNodeChange < 0) {
            gflags.put("use_node_to_node_encryption", nodeToNodeString);
            gflags.put("use_client_to_server_encryption", clientToNodeString);
            gflags.put("allow_insecure_connections", allowInsecureString);
            gflags.put("certs_dir", certsNodeDir);
          } else {
            LOG.warn("Round2 upgrade not required when there is no change in node-to-node");
          }

          subcommand.add("--replace_gflags");
          subcommand.add("--gflags");
          subcommand.add(Json.stringify(Json.toJson(gflags)));
        } else {
          throw new RuntimeException("Invalid taskSubType property: " + subType);
        }
        break;
    }
    return subcommand;
  }

  public ShellResponse nodeCommand(NodeCommandType type, NodeTaskParams nodeTaskParam) {
    List<String> commandArgs = new ArrayList<>();
    UserIntent userIntent = getUserIntentFromParams(nodeTaskParam);
    Path bootScriptFile = null;

    switch (type) {
      case Replace_Root_Volume:
        if (!(nodeTaskParam instanceof ReplaceRootVolume.Params)) {
          throw new RuntimeException("NodeTaskParams is not ReplaceRootVolume.Params");
        }

        ReplaceRootVolume.Params rrvParams = (ReplaceRootVolume.Params) nodeTaskParam;
        commandArgs.add("--replacement_disk");
        commandArgs.add(rrvParams.replacementDisk);
        commandArgs.addAll(getAccessKeySpecificCommand(rrvParams, type));
        break;
      case Create_Root_Volumes:
        if (!(nodeTaskParam instanceof CreateRootVolumes.Params)) {
          throw new RuntimeException("NodeTaskParams is not CreateRootVolumes.Params");
        }

        CreateRootVolumes.Params crvParams = (CreateRootVolumes.Params) nodeTaskParam;
        commandArgs.add("--num_disks");
        commandArgs.add(String.valueOf(crvParams.numVolumes));
        // intentional fall-thru
      case Provision:
        {
          if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
          }
          AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params) nodeTaskParam;
          Common.CloudType cloudType = userIntent.providerType;
          if (!cloudType.equals(Common.CloudType.onprem)) {
            commandArgs.add("--instance_type");
            commandArgs.add(taskParam.instanceType);
            commandArgs.add("--cloud_subnet");
            commandArgs.add(taskParam.subnetId);

            Config config = this.runtimeConfigFactory.forProvider(nodeTaskParam.getProvider());

            if (config.hasPath(BOOT_SCRIPT_PATH)) {
              String bootScript = config.getString(BOOT_SCRIPT_PATH);
              commandArgs.add("--boot_script");

              // treat the contents as script body if it starts with a shebang line
              // otherwise consider the contents to be a path
              if (bootScript.startsWith("#!")) {
                try {
                  bootScriptFile = Files.createTempFile(nodeTaskParam.nodeName, "-boot.sh");
                  Files.write(bootScriptFile, bootScript.getBytes());

                  commandArgs.add(bootScriptFile.toAbsolutePath().toString());
                } catch (IOException e) {
                  LOG.error(e.getMessage(), e);
                  throw new RuntimeException(e);
                }
              } else {
                commandArgs.add(bootScript);
              }
            }

            // For now we wouldn't add machine image for aws and fallback on the default
            // one devops gives us, we need to transition to having this use versioning
            // like base_image_version [ENG-1859]
            String ybImage =
                Optional.ofNullable(taskParam.machineImage).orElse(taskParam.getRegion().ybImage);
            if (ybImage != null && !ybImage.isEmpty()) {
              commandArgs.add("--machine_image");
              commandArgs.add(ybImage);
            }
            /*
            // TODO(bogdan): talk to Ram about this, if we want/use it for kube/onprem?
            if (!cloudType.equals(Common.CloudType.aws) && !cloudType.equals(Common.CloudType.gcp)) {
              commandArgs.add("--machine_image");
              commandArgs.add(taskParam.getRegion().ybImage);
            }
            */
            if (taskParam.assignPublicIP) {
              commandArgs.add("--assign_public_ip");
            }
          }

          if (taskParam.useTimeSync
              && (cloudType.equals(Common.CloudType.aws)
                  || cloudType.equals(Common.CloudType.gcp))) {
            commandArgs.add("--use_chrony");
          }

          if (cloudType.equals(Common.CloudType.aws)) {
            if (taskParam.cmkArn != null) {
              commandArgs.add("--cmk_res_name");
              commandArgs.add(taskParam.cmkArn);
            }

            if (taskParam.ipArnString != null) {
              commandArgs.add("--iam_profile_arn");
              commandArgs.add(taskParam.ipArnString);
            }

            if (!taskParam.remotePackagePath.isEmpty()) {
              commandArgs.add("--remote_package_path");
              commandArgs.add(taskParam.remotePackagePath);
            }
          }
          if (cloudType.equals(Common.CloudType.azu)) {
            Region r = taskParam.getRegion();
            String vnetName = r.getVnetName();
            if (vnetName != null && !vnetName.isEmpty()) {
              commandArgs.add("--vpcId");
              commandArgs.add(vnetName);
            }
          }

          if (Provider.InstanceTagsEnabledProviders.contains(cloudType)
              && userIntent.instanceTags != null
              && !userIntent.instanceTags.isEmpty()) {
            Map<String, String> useTags = userIntent.getInstanceTagsForInstanceOps();
            commandArgs.add("--instance_tags");
            commandArgs.add(Json.stringify(Json.toJson(useTags)));
          }

          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
            DeviceInfo deviceInfo = nodeTaskParam.deviceInfo;
            if (deviceInfo.storageType != null) {
              commandArgs.add("--volume_type");
              commandArgs.add(deviceInfo.storageType.toString().toLowerCase());
              if (deviceInfo.storageType.isIopsProvisioning() && deviceInfo.diskIops != null) {
                commandArgs.add("--disk_iops");
                commandArgs.add(Integer.toString(deviceInfo.diskIops));
              }
              if (deviceInfo.storageType.isThroughputProvisioning()
                  && deviceInfo.throughput != null) {

                commandArgs.add("--disk_throughput");
                commandArgs.add(Integer.toString(deviceInfo.throughput));
              }
            }
          }

          String localPackagePath = getThirdpartyPackagePath();
          if (localPackagePath != null) {
            commandArgs.add("--local_package_path");
            commandArgs.add(localPackagePath);
          }

          if (taskParam.reprovision) {
            commandArgs.add("--reuse_host");
            commandArgs.add("--reprovision");
          }

          break;
        }
      case Configure:
        {
          if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
          }
          AnsibleConfigureServers.Params taskParam = (AnsibleConfigureServers.Params) nodeTaskParam;
          commandArgs.addAll(getConfigureSubCommand(taskParam));
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          }
          break;
        }
      case List:
        {
          if (userIntent.providerType.equals(Common.CloudType.onprem)) {
            if (nodeTaskParam.deviceInfo != null) {
              commandArgs.addAll(getDeviceArgs(nodeTaskParam));
            }
            commandArgs.addAll(getAccessKeySpecificCommand(nodeTaskParam, type));
          }
          commandArgs.add("--as_json");
          break;
        }
      case Destroy:
        {
          if (!(nodeTaskParam instanceof AnsibleDestroyServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleDestroyServer.Params");
          }
          AnsibleDestroyServer.Params taskParam = (AnsibleDestroyServer.Params) nodeTaskParam;
          commandArgs = addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
          if (taskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(taskParam));
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Pause:
        {
          if (!(nodeTaskParam instanceof PauseServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not PauseServer.Params");
          }
          PauseServer.Params taskParam = (PauseServer.Params) nodeTaskParam;
          commandArgs = addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
          if (taskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(taskParam));
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Resume:
        {
          if (!(nodeTaskParam instanceof ResumeServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not ResumeServer.Params");
          }
          ResumeServer.Params taskParam = (ResumeServer.Params) nodeTaskParam;
          commandArgs = addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
          if (taskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(taskParam));
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Control:
        {
          if (!(nodeTaskParam instanceof AnsibleClusterServerCtl.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleClusterServerCtl.Params");
          }
          AnsibleClusterServerCtl.Params taskParam = (AnsibleClusterServerCtl.Params) nodeTaskParam;
          commandArgs.add(taskParam.process);
          commandArgs.add(taskParam.command);
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Tags:
        {
          if (!(nodeTaskParam instanceof InstanceActions.Params)) {
            throw new RuntimeException("NodeTaskParams is not InstanceActions.Params");
          }
          InstanceActions.Params taskParam = (InstanceActions.Params) nodeTaskParam;
          if (Provider.InstanceTagsEnabledProviders.contains(userIntent.providerType)) {
            if (userIntent.instanceTags == null || userIntent.instanceTags.isEmpty()) {
              throw new RuntimeException("Invalid instance tags");
            }
            Map<String, String> useTags = userIntent.getInstanceTagsForInstanceOps();
            commandArgs.add("--instance_tags");
            commandArgs.add(Json.stringify(Json.toJson(useTags)));
            if (!taskParam.deleteTags.isEmpty()) {
              commandArgs.add("--remove_tags");
              commandArgs.add(taskParam.deleteTags);
            }
            if (userIntent.providerType.equals(Common.CloudType.azu)) {
              commandArgs.addAll(getDeviceArgs(taskParam));
              commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
            }
          }
          break;
        }
      case Disk_Update:
        {
          if (!(nodeTaskParam instanceof InstanceActions.Params)) {
            throw new RuntimeException("NodeTaskParams is not InstanceActions.Params");
          }
          InstanceActions.Params taskParam = (InstanceActions.Params) nodeTaskParam;
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          commandArgs.add("--instance_type");
          commandArgs.add(taskParam.instanceType);
          if (taskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(taskParam));
          }
          break;
        }
      case CronCheck:
        {
          if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
          }
          commandArgs.addAll(getAccessKeySpecificCommand(nodeTaskParam, type));
        }
      case Precheck:
        {
          commandArgs.addAll(getAccessKeySpecificCommand(nodeTaskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          }
          break;
        }
    }
    commandArgs.add(nodeTaskParam.nodeName);
    try {
      return execCommand(
          nodeTaskParam.getRegion().uuid,
          null,
          null,
          type.toString().toLowerCase(),
          commandArgs,
          getCloudArgs(nodeTaskParam));
    } finally {
      if (bootScriptFile != null) {
        try {
          Files.deleteIfExists(bootScriptFile);
        } catch (IOException e) {
          LOG.error(e.getMessage(), e);
        }
      }
    }
  }

  private List<String> addArguments(List<String> commandArgs, String nodeIP, String instanceType) {
    commandArgs.add("--instance_type");
    commandArgs.add(instanceType);
    commandArgs.add("--node_ip");
    commandArgs.add(nodeIP);
    return commandArgs;
  }
}
