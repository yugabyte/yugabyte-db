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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.INodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ChangeInstanceType;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.PauseServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.commissioner.tasks.subtasks.ResumeServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.TransferXClusterCerts;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateMountedDisks;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.csv.CSVFormat;
import org.apache.commons.csv.CSVParser;
import org.apache.commons.csv.CSVPrinter;
import org.apache.commons.csv.CSVRecord;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.InetAddressValidator;
import org.bouncycastle.asn1.x509.GeneralName;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@Singleton
@Slf4j
public class NodeManager extends DevopsBase {
  static final String BOOT_SCRIPT_PATH = "yb.universe_boot_script";
  static final String BOOT_SCRIPT_TOKEN = "39666ab2-6633-4806-9685-5134321bd0d1";
  static final String BOOT_SCRIPT_COMPLETE =
      "\nsync\necho " + BOOT_SCRIPT_TOKEN + " >/etc/yb-boot-script-complete\n";
  private static final String YB_CLOUD_COMMAND_TYPE = "instance";
  public static final String CERT_LOCATION_NODE = "node";
  public static final String CERT_LOCATION_PLATFORM = "platform";
  private static final List<String> VALID_CONFIGURE_PROCESS_TYPES =
      ImmutableList.of(ServerType.MASTER.name(), ServerType.TSERVER.name());
  static final String VERIFY_SERVER_ENDPOINT_GFLAG = "verify_server_endpoint";
  static final String SKIP_CERT_VALIDATION = "yb.tls.skip_cert_validation";
  static final String POSTGRES_MAX_MEM_MB = "yb.dbmem.postgres.max_mem_mb";
  static final String YSQL_CGROUP_PATH = "/sys/fs/cgroup/memory/ysql";
  static final String CERTS_NODE_SUBDIR = "/yugabyte-tls-config";
  static final String CERT_CLIENT_NODE_SUBDIR = "/yugabyte-client-tls-config";
  public static final String SPECIAL_CHARACTERS = "[^a-zA-Z0-9_-]+";
  public static final Pattern SPECIAL_CHARACTERS_PATTERN = Pattern.compile(SPECIAL_CHARACTERS);
  public static final String UNDEFOK = "undefok";
  // DB internal glag to suppress going into shell mode and delete files on master removal.
  public static final String NOTIFY_PEER_OF_REMOVAL_FROM_CLUSTER =
      "notify_peer_of_removal_from_cluster";

  @Inject ReleaseManager releaseManager;

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  // We need to define the enum such that the lower case value matches the action.
  public enum NodeCommandType {
    Provision,
    Create,
    Configure,
    CronCheck,
    Destroy,
    List,
    Control,
    Precheck,
    Tags,
    InitYSQL,
    Disk_Update,
    Update_Mounted_Disks,
    Change_Instance_Type,
    Pause,
    Resume,
    Create_Root_Volumes,
    Replace_Root_Volume,
    Delete_Root_Volumes,
    Transfer_XCluster_Certs
  }

  public enum CertRotateAction {
    APPEND_NEW_ROOT_CERT,
    REMOVE_OLD_ROOT_CERT,
    ROTATE_CERTS,
    UPDATE_CERT_DIRS
  }

  public static final Logger LOG = LoggerFactory.getLogger(NodeManager.class);

  @Inject play.Configuration appConfig;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  private UserIntent getUserIntentFromParams(NodeTaskParams nodeTaskParam) {
    Universe universe = Universe.getOrBadRequest(nodeTaskParam.universeUUID);
    return getUserIntentFromParams(universe, nodeTaskParam);
  }

  private UserIntent getUserIntentFromParams(Universe universe, NodeTaskParams nodeTaskParam) {
    NodeDetails nodeDetails = universe.getNode(nodeTaskParam.nodeName);
    if (nodeDetails == null) {
      Iterator<NodeDetails> nodeIter = universe.getUniverseDetails().nodeDetailsSet.iterator();
      if (!nodeIter.hasNext()) {
        throw new RuntimeException("No node is found in universe " + universe.name);
      }
      nodeDetails = nodeIter.next();
      LOG.info("Node {} not found, so using {}.", nodeTaskParam.nodeName, nodeDetails.nodeName);
    }
    return universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid).userIntent;
  }

  private List<String> getCloudArgs(NodeTaskParams nodeTaskParam) {
    List<String> command = new ArrayList<>();
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
      // Instance may not be present if it is deleted from NodeInstance table after a release
      // action. Node UUID is not available in 2.6.
      Optional<NodeInstance> node = NodeInstance.maybeGetByName(nodeTaskParam.getNodeName());
      command.add("--node_metadata");
      command.add(node.isPresent() ? node.get().getDetailsJson() : "{}");
    }
    return command;
  }

  private List<String> getAccessKeySpecificCommand(NodeTaskParams params, NodeCommandType type) {
    List<String> subCommand = new ArrayList<>();
    if (params.universeUUID == null) {
      throw new RuntimeException("NodeTaskParams missing Universe UUID.");
    }
    UserIntent userIntent = getUserIntentFromParams(params);
    final String defaultAccessKeyCode = appConfig.getString("yb.security.default.access.key");

    // TODO: [ENG-1242] we shouldn't be using our keypair, until we fix our VPC to support VPN
    if (userIntent != null && !userIntent.accessKeyCode.equalsIgnoreCase(defaultAccessKeyCode)) {
      AccessKey accessKey =
          AccessKey.getOrBadRequest(params.getProvider().uuid, userIntent.accessKeyCode);
      AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
      subCommand.addAll(
          getAccessKeySpecificCommand(
              params, type, keyInfo, userIntent.providerType, userIntent.accessKeyCode));
    }

    return subCommand;
  }

  private List<String> getAccessKeySpecificCommand(
      INodeTaskParams params,
      NodeCommandType type,
      AccessKey.KeyInfo keyInfo,
      Common.CloudType providerType,
      String accessKeyCode) {
    List<String> subCommand = new ArrayList<>();

    if (keyInfo.vaultFile != null) {
      subCommand.add("--vars_file");
      subCommand.add(keyInfo.vaultFile);
      subCommand.add("--vault_password_file");
      subCommand.add(keyInfo.vaultPasswordFile);
    }
    if (keyInfo.privateKey != null) {
      subCommand.add("--private_key_file");
      subCommand.add(keyInfo.privateKey);

      // We only need to include keyPair name for create instance method and if this is aws.
      if ((params instanceof AnsibleCreateServer.Params
              || params instanceof AnsibleSetupServer.Params)
          && providerType.equals(Common.CloudType.aws)) {
        subCommand.add("--key_pair_name");
        subCommand.add(accessKeyCode);
        // Also we will add the security group information for create
        if (params instanceof AnsibleCreateServer.Params) {
          Region r = params.getRegion();
          String customSecurityGroupId = r.getSecurityGroupId();
          if (customSecurityGroupId != null) {
            subCommand.add("--security_group_id");
            subCommand.add(customSecurityGroupId);
          }
        }
      }
    }
    // security group is only used during Azure create instance method
    if (params instanceof AnsibleCreateServer.Params && providerType.equals(Common.CloudType.azu)) {
      Region r = params.getRegion();
      String customSecurityGroupId = r.getSecurityGroupId();
      if (customSecurityGroupId != null) {
        subCommand.add("--security_group_id");
        subCommand.add(customSecurityGroupId);
      }
    }

    if (params instanceof AnsibleDestroyServer.Params
        && providerType.equals(Common.CloudType.onprem)) {
      subCommand.add("--install_node_exporter");
    }

    subCommand.add("--custom_ssh_port");
    subCommand.add(keyInfo.sshPort.toString());

    if ((type == NodeCommandType.Provision
            || type == NodeCommandType.Destroy
            || type == NodeCommandType.Create
            || type == NodeCommandType.Disk_Update
            || type == NodeCommandType.Update_Mounted_Disks)
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

      if (keyInfo.setUpChrony) {
        subCommand.add("--skip_ntp_check");
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

      if (keyInfo.setUpChrony) {
        subCommand.add("--use_chrony");
        if (keyInfo.ntpServers != null && !keyInfo.ntpServers.isEmpty()) {
          for (String server : keyInfo.ntpServers) {
            subCommand.add("--ntp_server");
            subCommand.add(server);
          }
        }
      }

      // Legacy providers should not be allowed to have no NTP set up. See PLAT 4015
      if (!keyInfo.showSetUpChrony
          && !keyInfo.airGapInstall
          && !((AnsibleSetupServer.Params) params).useTimeSync
          && (providerType.equals(Common.CloudType.aws)
              || providerType.equals(Common.CloudType.gcp)
              || providerType.equals(Common.CloudType.azu))) {
        subCommand.add("--use_chrony");
        List<String> publicServerList =
            Arrays.asList("0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org", "3.pool.ntp.org");
        for (String server : publicServerList) {
          subCommand.add("--ntp_server");
          subCommand.add(server);
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

  /**
   * Creates certificates if not present. Called from various places like - when node is added to
   * universe
   */
  private List<String> getCertificatePaths(
      Config config,
      UserIntent userIntent,
      AnsibleConfigureServers.Params taskParam,
      String nodeIP,
      String ybHomeDir,
      Map<String, Integer> subjectAltName) {
    return getCertificatePaths(
        config,
        userIntent,
        taskParam,
        EncryptionInTransitUtil.isRootCARequired(taskParam),
        EncryptionInTransitUtil.isClientRootCARequired(taskParam),
        nodeIP,
        ybHomeDir,
        subjectAltName);
  }

  // Return the List of Strings which gives the certificate paths for the specific taskParams
  private List<String> getCertificatePaths(
      Config config,
      UserIntent userIntent,
      AnsibleConfigureServers.Params taskParam,
      boolean isRootCARequired,
      boolean isClientRootCARequired,
      String commonName,
      String ybHomeDir,
      Map<String, Integer> subjectAltName) {
    List<String> subcommandStrings = new ArrayList<>();

    String serverCertFile = String.format("node.%s.crt", commonName);
    String serverKeyFile = String.format("node.%s.key", commonName);

    if (isRootCARequired) {
      subcommandStrings.add("--certs_node_dir");
      subcommandStrings.add(getCertsNodeDir(ybHomeDir));

      CertificateInfo rootCert = CertificateInfo.get(taskParam.rootCA);
      if (rootCert == null) {
        throw new RuntimeException("No valid rootCA found for " + taskParam.universeUUID);
      }

      String rootCertPath, serverCertPath, serverKeyPath, certsLocation;

      switch (rootCert.certType) {
        case SelfSigned:
        case HashicorpVault:
          {
            try {
              // Creating a temp directory to save Server Cert and Key from Root for the node
              Path tempStorageDirectory;
              if (rootCert.certType == CertConfigType.SelfSigned) {
                tempStorageDirectory =
                    Files.createTempDirectory(String.format("SelfSigned%s", taskParam.rootCA))
                        .toAbsolutePath();
              } else {
                tempStorageDirectory =
                    Files.createTempDirectory(String.format("Hashicorp%s", taskParam.rootCA))
                        .toAbsolutePath();
              }
              CertificateHelper.createServerCertificate(
                  taskParam.rootCA,
                  tempStorageDirectory.toString(),
                  commonName,
                  null,
                  null,
                  serverCertFile,
                  serverKeyFile,
                  subjectAltName);
              rootCertPath = rootCert.certificate;
              serverCertPath = String.format("%s/%s", tempStorageDirectory, serverCertFile);
              serverKeyPath = String.format("%s/%s", tempStorageDirectory, serverKeyFile);
              certsLocation = CERT_LOCATION_PLATFORM;

              if (taskParam.rootAndClientRootCASame && taskParam.enableClientToNodeEncrypt) {
                // These client certs are used for node to postgres communication
                // These are separate from clientRoot certs which are used for server to client
                // communication These are not required anymore as this is not mandatory now and
                // can be removed. The code is still here to maintain backward compatibility
                subcommandStrings.add("--client_cert_path");
                subcommandStrings.add(CertificateHelper.getClientCertFile(taskParam.rootCA));
                subcommandStrings.add("--client_key_path");
                subcommandStrings.add(CertificateHelper.getClientKeyFile(taskParam.rootCA));
              }
            } catch (IOException e) {
              LOG.error(e.getMessage(), e);
              throw new RuntimeException(e);
            }
            break;
          }
        case CustomCertHostPath:
          {
            CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertPathParams();
            rootCertPath = customCertInfo.rootCertPath;
            serverCertPath = customCertInfo.nodeCertPath;
            serverKeyPath = customCertInfo.nodeKeyPath;
            certsLocation = CERT_LOCATION_NODE;
            if (taskParam.rootAndClientRootCASame
                && taskParam.enableClientToNodeEncrypt
                && customCertInfo.clientCertPath != null
                && !customCertInfo.clientCertPath.isEmpty()
                && customCertInfo.clientKeyPath != null
                && !customCertInfo.clientKeyPath.isEmpty()) {
              // These client certs are used for node to postgres communication
              // These are seprate from clientRoot certs which are used for server to client
              // communication These are not required anymore as this is not mandatory now and
              // can be removed
              // The code is still here to mantain backward compatibility
              subcommandStrings.add("--client_cert_path");
              subcommandStrings.add(customCertInfo.clientCertPath);
              subcommandStrings.add("--client_key_path");
              subcommandStrings.add(customCertInfo.clientKeyPath);
            }
            break;
          }
        case CustomServerCert:
          {
            throw new RuntimeException("rootCA cannot be of type CustomServerCert.");
          }
        default:
          {
            throw new RuntimeException("certType should be valid.");
          }
      }

      // These Server Certs are used for TLS Encryption for Node to Node and
      // (in legacy nodes) client to node as well
      subcommandStrings.add("--root_cert_path");
      subcommandStrings.add(rootCertPath);
      subcommandStrings.add("--server_cert_path");
      subcommandStrings.add(serverCertPath);
      subcommandStrings.add("--server_key_path");
      subcommandStrings.add(serverKeyPath);
      subcommandStrings.add("--certs_location");
      subcommandStrings.add(certsLocation);
    }
    if (isClientRootCARequired) {
      subcommandStrings.add("--certs_client_dir");
      subcommandStrings.add(getCertsForClientDir(ybHomeDir));

      CertificateInfo clientRootCert = CertificateInfo.get(taskParam.clientRootCA);
      if (clientRootCert == null) {
        throw new RuntimeException("No valid clientRootCA found for " + taskParam.universeUUID);
      }

      String rootCertPath, serverCertPath, serverKeyPath, certsLocation;

      switch (clientRootCert.certType) {
        case SelfSigned:
        case HashicorpVault:
          {
            try {
              // Creating a temp directory to save Server Cert and Key from Root for the node
              Path tempStorageDirectory;
              if (clientRootCert.certType == CertConfigType.SelfSigned) {
                tempStorageDirectory =
                    Files.createTempDirectory(String.format("SelfSigned%s", taskParam.rootCA))
                        .toAbsolutePath();
              } else {
                tempStorageDirectory =
                    Files.createTempDirectory(String.format("Hashicorp%s", taskParam.rootCA))
                        .toAbsolutePath();
              }
              CertificateHelper.createServerCertificate(
                  taskParam.clientRootCA,
                  tempStorageDirectory.toString(),
                  commonName,
                  null,
                  null,
                  serverCertFile,
                  serverKeyFile,
                  subjectAltName);
              rootCertPath = clientRootCert.certificate;
              serverCertPath = String.format("%s/%s", tempStorageDirectory, serverCertFile);
              serverKeyPath = String.format("%s/%s", tempStorageDirectory, serverKeyFile);
              certsLocation = CERT_LOCATION_PLATFORM;
            } catch (IOException e) {
              LOG.error(e.getMessage(), e);
              throw new RuntimeException(e);
            }
            break;
          }
        case CustomCertHostPath:
          {
            CertificateParams.CustomCertInfo customCertInfo =
                clientRootCert.getCustomCertPathParams();
            rootCertPath = customCertInfo.rootCertPath;
            serverCertPath = customCertInfo.nodeCertPath;
            serverKeyPath = customCertInfo.nodeKeyPath;
            certsLocation = CERT_LOCATION_NODE;
            break;
          }
        case CustomServerCert:
          {
            CertificateInfo.CustomServerCertInfo customServerCertInfo =
                clientRootCert.getCustomServerCertInfo();
            rootCertPath = clientRootCert.certificate;
            serverCertPath = customServerCertInfo.serverCert;
            serverKeyPath = customServerCertInfo.serverKey;
            certsLocation = CERT_LOCATION_PLATFORM;
            break;
          }
        default:
          {
            throw new RuntimeException("certType should be valid.");
          }
      }

      // These Server Certs are used for TLS Encryption for Client to Node
      subcommandStrings.add("--root_cert_path_client_to_server");
      subcommandStrings.add(rootCertPath);
      subcommandStrings.add("--server_cert_path_client_to_server");
      subcommandStrings.add(serverCertPath);
      subcommandStrings.add("--server_key_path_client_to_server");
      subcommandStrings.add(serverKeyPath);
      subcommandStrings.add("--certs_location_client_to_server");
      subcommandStrings.add(certsLocation);
    }

    SkipCertValidationType skipType = getSkipCertValidationType(config, userIntent, taskParam);
    if (skipType != SkipCertValidationType.NONE) {
      subcommandStrings.add("--skip_cert_validation");
      subcommandStrings.add(skipType.name());
    }

    return subcommandStrings;
  }

  private static String getCertsNodeDir(String ybHomeDir) {
    return ybHomeDir + CERTS_NODE_SUBDIR;
  }

  private static String getCertsForClientDir(String ybHomeDir) {
    return ybHomeDir + CERT_CLIENT_NODE_SUBDIR;
  }

  private String getMountPoints(AnsibleConfigureServers.Params taskParam) {
    if (taskParam.deviceInfo.mountPoints != null) {
      return taskParam.deviceInfo.mountPoints;
    } else if (taskParam.deviceInfo.numVolumes != null
        && !taskParam.getProvider().code.equals("onprem")) {
      List<String> mountPoints = new ArrayList<>();
      for (int i = 0; i < taskParam.deviceInfo.numVolumes; i++) {
        mountPoints.add("/mnt/d" + i);
      }
      return String.join(",", mountPoints);
    }
    return null;
  }

  private Map<String, String> getCertsAndTlsGFlags(AnsibleConfigureServers.Params taskParam) {
    Map<String, String> gflags = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String nodeToNodeString = String.valueOf(taskParam.enableNodeToNodeEncrypt);
    String clientToNodeString = String.valueOf(taskParam.enableClientToNodeEncrypt);
    String allowInsecureString = String.valueOf(taskParam.allowInsecure);
    String ybHomeDir = taskParam.getProvider().getYbHome();
    String certsDir = getCertsNodeDir(ybHomeDir);
    String certsForClientDir = getCertsForClientDir(ybHomeDir);

    gflags.put("use_node_to_node_encryption", nodeToNodeString);
    gflags.put("use_client_to_server_encryption", clientToNodeString);
    gflags.put("allow_insecure_connections", allowInsecureString);
    if (taskParam.enableClientToNodeEncrypt || taskParam.enableNodeToNodeEncrypt) {
      gflags.put("cert_node_filename", node.cloudInfo.private_ip);
    }
    if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
      gflags.put("certs_dir", certsDir);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
      gflags.put("certs_for_client_dir", certsForClientDir);
    }
    return gflags;
  }

  private Map<String, String> getYSQLGFlags(
      AnsibleConfigureServers.Params taskParam, Boolean useHostname, Boolean useSecondaryIp) {
    Map<String, String> gflags = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String pgsqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      pgsqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYSQL) {
      gflags.put("enable_ysql", "true");
      gflags.put(
          "pgsql_proxy_bind_address",
          String.format("%s:%s", pgsqlProxyBindAddress, node.ysqlServerRpcPort));
      gflags.put("pgsql_proxy_webserver_port", Integer.toString(node.ysqlServerHttpPort));
      if (taskParam.enableYSQLAuth) {
        gflags.put("ysql_enable_auth", "true");
        gflags.put("ysql_hba_conf_csv", "local all yugabyte trust");
      } else {
        gflags.put("ysql_enable_auth", "false");
      }
    } else {
      gflags.put("enable_ysql", "false");
    }
    return gflags;
  }

  private Map<String, String> getYCQLGFlags(
      AnsibleConfigureServers.Params taskParam, Boolean useHostname, Boolean useSecondaryIp) {
    Map<String, String> gflags = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String cqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      cqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYCQL) {
      gflags.put("start_cql_proxy", "true");
      gflags.put(
          "cql_proxy_bind_address",
          String.format("%s:%s", cqlProxyBindAddress, node.yqlServerRpcPort));
      gflags.put("cql_proxy_webserver_port", Integer.toString(node.yqlServerHttpPort));
      if (taskParam.enableYCQLAuth) {
        gflags.put("use_cassandra_authentication", "true");
      } else {
        gflags.put("use_cassandra_authentication", "false");
      }
    } else {
      gflags.put("start_cql_proxy", "false");
    }
    return gflags;
  }

  private Map<String, String> getMasterDefaultGFlags(
      AnsibleConfigureServers.Params taskParam,
      Boolean useHostname,
      Boolean useSecondaryIp,
      Boolean isDualNet) {
    Map<String, String> gflags = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String private_ip = node.cloudInfo.private_ip;

    if (useHostname) {
      gflags.put(
          "server_broadcast_addresses", String.format("%s:%s", private_ip, node.masterRpcPort));
    } else {
      gflags.put("server_broadcast_addresses", "");
    }

    if (!taskParam.isMasterInShellMode) {
      gflags.put("master_addresses", masterAddresses);
    } else {
      gflags.put("master_addresses", "");
    }

    gflags.put("rpc_bind_addresses", String.format("%s:%s", private_ip, node.masterRpcPort));

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.masterRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.masterRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put("rpc_bind_addresses", bindAddresses);
    } else if (isDualNet) {
      gflags.put("use_private_ip", "cloud");
    }

    gflags.put("webserver_port", Integer.toString(node.masterHttpPort));
    gflags.put("webserver_interface", private_ip);
    boolean notifyPeerOnRemoval =
        runtimeConfigFactory
            .forUniverse(universe)
            .getBoolean("yb.gflags.notify_peer_of_removal_from_cluster");
    if (!notifyPeerOnRemoval) {
      // By default, it is true in the DB.
      gflags.put(NOTIFY_PEER_OF_REMOVAL_FROM_CLUSTER, String.valueOf(notifyPeerOnRemoval));
      gflags.put(UNDEFOK, NOTIFY_PEER_OF_REMOVAL_FROM_CLUSTER);
    }
    return gflags;
  }

  private Map<String, String> getTServerDefaultGflags(
      AnsibleConfigureServers.Params taskParam,
      Boolean useHostname,
      Boolean useSecondaryIp,
      Boolean isDualNet) {
    Map<String, String> gflags = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String private_ip = node.cloudInfo.private_ip;
    UserIntent userIntent = getUserIntentFromParams(taskParam);

    if (useHostname) {
      gflags.put(
          "server_broadcast_addresses", String.format("%s:%s", private_ip, node.tserverRpcPort));
    } else {
      gflags.put("server_broadcast_addresses", "");
    }
    gflags.put("rpc_bind_addresses", String.format("%s:%s", private_ip, node.tserverRpcPort));
    gflags.put("tserver_master_addrs", masterAddresses);

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.tserverRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.tserverRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put("rpc_bind_addresses", bindAddresses);
    } else if (isDualNet) {
      // We want the broadcast address to be secondary so that
      // it gets populated correctly for the client discovery tables.
      gflags.put("server_broadcast_addresses", node.cloudInfo.secondary_private_ip);
      gflags.put("use_private_ip", "cloud");
    }

    gflags.put("webserver_port", Integer.toString(node.tserverHttpPort));
    gflags.put("webserver_interface", private_ip);
    gflags.put(
        "redis_proxy_bind_address", String.format("%s:%s", private_ip, node.redisServerRpcPort));
    if (userIntent.enableYEDIS) {
      gflags.put(
          "redis_proxy_webserver_port",
          Integer.toString(taskParam.communicationPorts.redisServerHttpPort));
    } else {
      gflags.put("start_redis_proxy", "false");
    }
    if (runtimeConfigFactory.forUniverse(universe).getInt(POSTGRES_MAX_MEM_MB) > 0) {
      gflags.put("postmaster_cgroup", YSQL_CGROUP_PATH);
    }
    return gflags;
  }

  private static String mergeCSVs(String csv1, String csv2) {
    StringWriter writer = new StringWriter();
    try {
      CSVFormat csvFormat = CSVFormat.DEFAULT.builder().setRecordSeparator("").build();
      try (CSVPrinter csvPrinter = new CSVPrinter(writer, csvFormat)) {
        Set<String> records = new LinkedHashSet<>();
        CSVParser parser = new CSVParser(new StringReader(csv1), csvFormat);
        for (CSVRecord record : parser) {
          records.addAll(record.toList());
        }
        parser = new CSVParser(new StringReader(csv2), csvFormat);
        for (CSVRecord record : parser) {
          records.addAll(record.toList());
        }
        csvPrinter.printRecord(records);
        csvPrinter.flush();
      }
    } catch (IOException ignored) {
      // can't really happen
    }
    return writer.toString();
  }

  private static void mergeCSVs(Map<String, String> des, Map<String, String> src, String key) {
    if (des.containsKey(key)) {
      String csv = des.get(key);
      des.put(key, mergeCSVs(csv, src.getOrDefault(key, "")));
    }
  }

  /** Return the map of default gflags which will be passed as extra gflags to the db nodes. */
  private Map<String, String> getAllDefaultGFlags(
      AnsibleConfigureServers.Params taskParam, Boolean useHostname, Config config) {
    Map<String, String> extra_gflags = new HashMap<>();
    extra_gflags.put("placement_cloud", taskParam.getProvider().code);
    extra_gflags.put("placement_region", taskParam.getRegion().code);
    extra_gflags.put("placement_zone", taskParam.getAZ().code);
    extra_gflags.put("max_log_size", "256");
    extra_gflags.put(UNDEFOK, "enable_ysql");
    extra_gflags.put("metric_node_name", taskParam.nodeName);
    extra_gflags.put("placement_uuid", String.valueOf(taskParam.placementUuid));

    String mountPoints = getMountPoints(taskParam);
    if (mountPoints != null && mountPoints.length() > 0) {
      extra_gflags.put("fs_data_dirs", mountPoints);
    } else {
      throw new RuntimeException("mountpoints and numVolumes are missing from taskParam");
    }

    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    boolean useSecondaryIp = false;
    boolean legacyNet =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("true");
    boolean isDualNet =
        config.getBoolean("yb.cloud.enabled")
            && node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    if (isDualNet && !legacyNet) {
      useSecondaryIp = true;
    }

    String processType = taskParam.getProperty("processType");
    if (processType == null) {
      extra_gflags.put("master_addresses", "");
    } else if (processType.equals(ServerType.TSERVER.name())) {
      extra_gflags.putAll(
          getTServerDefaultGflags(taskParam, useHostname, useSecondaryIp, isDualNet));
    } else {
      Map<String, String> masterGFlags =
          getMasterDefaultGFlags(taskParam, useHostname, useSecondaryIp, isDualNet);
      // Merge into masterGFlags.
      mergeCSVs(masterGFlags, extra_gflags, UNDEFOK);
      extra_gflags.putAll(masterGFlags);
    }

    UserIntent userIntent = getUserIntentFromParams(taskParam);

    // Set on both master and tserver processes to allow db to validate inter-node RPCs.
    extra_gflags.put("cluster_uuid", String.valueOf(taskParam.universeUUID));
    if (taskParam.isMaster) {
      extra_gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
    }

    if (taskParam.getCurrentClusterType() == UniverseDefinitionTaskParams.ClusterType.PRIMARY
        && taskParam.setTxnTableWaitCountFlag) {
      extra_gflags.put(
          "txn_table_wait_min_ts_count",
          Integer.toString(universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes));
    }

    if (taskParam.callhomeLevel != null) {
      extra_gflags.put(
          "callhome_collection_level", taskParam.callhomeLevel.toString().toLowerCase());
      if (taskParam.callhomeLevel.toString().equals("NONE")) {
        extra_gflags.put("callhome_enabled", "false");
      }
    }

    extra_gflags.putAll(getYSQLGFlags(taskParam, useHostname, useSecondaryIp));
    extra_gflags.putAll(getYCQLGFlags(taskParam, useHostname, useSecondaryIp));
    extra_gflags.putAll(getCertsAndTlsGFlags(taskParam));
    return extra_gflags;
  }

  private List<String> getConfigureSubCommand(AnsibleConfigureServers.Params taskParam) {
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    Config config = runtimeConfigFactory.forUniverse(universe);
    UserIntent userIntent = getUserIntentFromParams(universe, taskParam);
    List<String> subcommand = new ArrayList<>();
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
        if (releaseMetadata.s3 != null) {
          subcommand.add("--s3_remote_download");
          ybServerPackage = releaseMetadata.s3.paths.x86_64;
        } else if (releaseMetadata.gcs != null) {
          subcommand.add("--gcs_remote_download");
          ybServerPackage = releaseMetadata.gcs.paths.x86_64;
        } else if (releaseMetadata.http != null) {
          subcommand.add("--http_remote_download");
          ybServerPackage = releaseMetadata.http.paths.x86_64;
          subcommand.add("--http_package_checksum");
          subcommand.add(releaseMetadata.http.paths.x86_64_checksum);
        } else {
          ybServerPackage = releaseMetadata.getFilePath(taskParam.getRegion());
        }
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

    // Custom cluster creation flow with prebuilt AMI for cloud
    if (taskParam.type != UpgradeTaskParams.UpgradeTaskType.Software) {
      maybeAddVMImageCommandArgs(
          universe,
          userIntent.providerType,
          taskParam.vmUpgradeTaskType,
          !taskParam.ignoreUseCustomImageConfig,
          subcommand);
    }

    boolean useHostname =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname
            || !isIpAddress(node.cloudInfo.private_ip);

    Map<String, Integer> alternateNames = new HashMap<>();
    String commonName = node.cloudInfo.private_ip;
    alternateNames.put(
        node.cloudInfo.private_ip, useHostname ? GeneralName.dNSName : GeneralName.iPAddress);
    if (node.cloudInfo.secondary_private_ip != null
        && !node.cloudInfo.secondary_private_ip.equals("null")) {
      commonName = node.cloudInfo.secondary_private_ip;
      alternateNames.put(node.cloudInfo.secondary_private_ip, GeneralName.iPAddress);
    }

    switch (taskParam.type) {
      case Everything:
        if (ybServerPackage == null) {
          throw new RuntimeException(
              "Unable to fetch yugabyte release for version: " + taskParam.ybSoftwareVersion);
        }
        subcommand.add("--package");
        subcommand.add(ybServerPackage);
        subcommand.add("--num_releases_to_keep");
        if (config.getBoolean("yb.cloud.enabled")) {
          subcommand.add(
              runtimeConfigFactory
                  .forUniverse(universe)
                  .getString("yb.releases.num_releases_to_keep_cloud"));
        } else {
          subcommand.add(
              runtimeConfigFactory
                  .forUniverse(universe)
                  .getString("yb.releases.num_releases_to_keep_default"));
        }
        if ((taskParam.enableNodeToNodeEncrypt || taskParam.enableClientToNodeEncrypt)) {
          subcommand.addAll(
              getCertificatePaths(
                  config,
                  userIntent,
                  taskParam,
                  commonName,
                  taskParam.getProvider().getYbHome(),
                  alternateNames));
        }
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
          } else if (taskSubType.equals(UpgradeTaskParams.UpgradeTaskSubType.Download.toString())) {
            subcommand.add("--tags");
            subcommand.add("download-software");
          } else if (taskSubType.equals(UpgradeTaskParams.UpgradeTaskSubType.Install.toString())) {
            subcommand.add("--tags");
            subcommand.add("install-software");
          }
          subcommand.add("--num_releases_to_keep");
          if (config.getBoolean("yb.cloud.enabled")) {
            subcommand.add(
                runtimeConfigFactory
                    .forUniverse(universe)
                    .getString("yb.releases.num_releases_to_keep_cloud"));
          } else {
            subcommand.add(
                runtimeConfigFactory
                    .forUniverse(universe)
                    .getString("yb.releases.num_releases_to_keep_default"));
          }
        }
        break;
      case GFlags:
        {
          String processType = taskParam.getProperty("processType");
          if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
            throw new RuntimeException("Invalid processType: " + processType);
          } else {
            subcommand.add("--yb_process_type");
            subcommand.add(processType.toLowerCase());
          }

          // TODO: PLAT-2782: certificates are generated 3 times for each node.
          if ((taskParam.enableNodeToNodeEncrypt || taskParam.enableClientToNodeEncrypt)) {
            subcommand.addAll(
                getCertificatePaths(
                    runtimeConfigFactory.forUniverse(universe),
                    userIntent,
                    taskParam,
                    commonName,
                    taskParam.getProvider().getYbHome(),
                    alternateNames));
          }

          Map<String, String> gflags = new HashMap<>(taskParam.gflags);
          subcommand.add("--gflags");
          subcommand.add(Json.stringify(Json.toJson(gflags)));

          subcommand.add("--tags");
          subcommand.add("override_gflags");
        }
        break;
      case Certs:
        {
          if (taskParam.certRotateAction == null) {
            throw new RuntimeException("Cert Rotation Action is null.");
          }

          String processType = taskParam.getProperty("processType");
          if (processType == null || !VALID_CONFIGURE_PROCESS_TYPES.contains(processType)) {
            throw new RuntimeException("Invalid processType: " + processType);
          } else {
            subcommand.add("--yb_process_type");
            subcommand.add(processType.toLowerCase());
          }

          String ybHomeDir =
              Provider.getOrBadRequest(
                      UUID.fromString(
                          universe.getUniverseDetails().getPrimaryCluster().userIntent.provider))
                  .getYbHome();
          String certsNodeDir = getCertsNodeDir(ybHomeDir);
          String certsForClientDir = getCertsForClientDir(ybHomeDir);

          subcommand.add("--cert_rotate_action");
          subcommand.add(taskParam.certRotateAction.toString());

          CertificateInfo rootCert = null;
          if (taskParam.rootCA != null) {
            rootCert = CertificateInfo.get(taskParam.rootCA);
          }

          switch (taskParam.certRotateAction) {
            case APPEND_NEW_ROOT_CERT:
            case REMOVE_OLD_ROOT_CERT:
              {
                if (taskParam.rootCARotationType != CertRotationType.RootCert) {
                  throw new RuntimeException(
                      taskParam.certRotateAction
                          + " is needed only when there is rootCA rotation.");
                }
                if (rootCert == null) {
                  throw new RuntimeException("Certificate is null: " + taskParam.rootCA);
                }
                if (rootCert.certType == CertConfigType.CustomServerCert) {
                  throw new RuntimeException(
                      "Root certificate cannot be of type CustomServerCert.");
                }

                String rootCertPath = "";
                String certsLocation = "";
                if (rootCert.certType == CertConfigType.SelfSigned) {
                  rootCertPath = rootCert.certificate;
                  certsLocation = CERT_LOCATION_PLATFORM;
                } else if (rootCert.certType == CertConfigType.CustomCertHostPath) {
                  rootCertPath = rootCert.getCustomCertPathParams().rootCertPath;
                  certsLocation = CERT_LOCATION_NODE;
                } else if (rootCert.certType == CertConfigType.HashicorpVault) {
                  rootCertPath = rootCert.certificate;
                  certsLocation = CERT_LOCATION_PLATFORM;
                }

                subcommand.add("--root_cert_path");
                subcommand.add(rootCertPath);
                subcommand.add("--certs_location");
                subcommand.add(certsLocation);
                subcommand.add("--certs_node_dir");
                subcommand.add(certsNodeDir);
              }
              break;
            case ROTATE_CERTS:
              {
                subcommand.addAll(
                    getCertificatePaths(
                        config,
                        userIntent,
                        taskParam,
                        taskParam.rootCARotationType != CertRotationType.None,
                        taskParam.clientRootCARotationType != CertRotationType.None,
                        commonName,
                        ybHomeDir,
                        alternateNames));
              }
              break;
            case UPDATE_CERT_DIRS:
              {
                Map<String, String> gflags = new HashMap<>(taskParam.gflags);
                if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
                  gflags.put("certs_dir", certsNodeDir);
                }
                if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
                  gflags.put("certs_for_client_dir", certsForClientDir);
                }
                subcommand.add("--gflags");
                subcommand.add(Json.stringify(Json.toJson(gflags)));
                subcommand.add("--tags");
                subcommand.add("override_gflags");
                break;
              }
          }
        }
        break;
      case ToggleTls:
        {
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

          String ybHomeDir =
              Provider.getOrBadRequest(
                      UUID.fromString(
                          universe.getUniverseDetails().getPrimaryCluster().userIntent.provider))
                  .getYbHome();
          String certsDir = getCertsNodeDir(ybHomeDir);
          String certsForClientDir = getCertsForClientDir(ybHomeDir);

          if (UpgradeTaskParams.UpgradeTaskSubType.CopyCerts.name().equals(subType)) {
            if (taskParam.enableNodeToNodeEncrypt || taskParam.enableClientToNodeEncrypt) {
              subcommand.add("--cert_rotate_action");
              subcommand.add(CertRotateAction.ROTATE_CERTS.toString());
            }
            subcommand.addAll(
                getCertificatePaths(
                    config, userIntent, taskParam, commonName, ybHomeDir, alternateNames));

          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round1GFlagsUpdate.name()
              .equals(subType)) {
            Map<String, String> gflags = new HashMap<>(taskParam.gflags);
            if (taskParam.nodeToNodeChange > 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", "true");
              if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
                gflags.put("certs_dir", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            } else if (taskParam.nodeToNodeChange < 0) {
              gflags.put("allow_insecure_connections", "true");
            } else {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
                gflags.put("certs_dir", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            }

            subcommand.add("--gflags");
            subcommand.add(Json.stringify(Json.toJson(gflags)));

            subcommand.add("--tags");
            subcommand.add("override_gflags");

          } else if (UpgradeTaskParams.UpgradeTaskSubType.Round2GFlagsUpdate.name()
              .equals(subType)) {
            Map<String, String> gflags = new HashMap<>(taskParam.gflags);
            if (taskParam.nodeToNodeChange > 0) {
              gflags.put("allow_insecure_connections", allowInsecureString);
            } else if (taskParam.nodeToNodeChange < 0) {
              gflags.put("use_node_to_node_encryption", nodeToNodeString);
              gflags.put("use_client_to_server_encryption", clientToNodeString);
              gflags.put("allow_insecure_connections", allowInsecureString);
              if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
                gflags.put("certs_dir", certsDir);
              }
              if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
                gflags.put("certs_for_client_dir", certsForClientDir);
              }
            } else {
              LOG.warn("Round2 upgrade not required when there is no change in node-to-node");
            }
            subcommand.add("--gflags");
            subcommand.add(Json.stringify(Json.toJson(gflags)));

            subcommand.add("--tags");
            subcommand.add("override_gflags");
          } else {
            throw new RuntimeException("Invalid taskSubType property: " + subType);
          }
        }
        break;
    }

    // extra_gflags is the base set of gflags that is common to all tasks.
    // These can be overriden by  gflags which contain task-specific overrides.
    // User set flags are added to gflags, so if user specifies any of the gflags set here, they
    // will take precedence over our base set.
    subcommand.add("--extra_gflags");
    subcommand.add(
        Json.stringify(Json.toJson(getAllDefaultGFlags(taskParam, useHostname, config))));
    return subcommand;
  }

  static boolean isIpAddress(String maybeIp) {
    InetAddressValidator ipValidator = InetAddressValidator.getInstance();
    return ipValidator.isValidInet4Address(maybeIp) || ipValidator.isValidInet6Address(maybeIp);
  }

  enum SkipCertValidationType {
    ALL,
    HOSTNAME,
    NONE
  }

  @VisibleForTesting
  static SkipCertValidationType getSkipCertValidationType(
      Config config, UserIntent userIntent, AnsibleConfigureServers.Params taskParam) {
    return getSkipCertValidationType(
        config, userIntent, taskParam.gflags, taskParam.gflagsToRemove);
  }

  private static SkipCertValidationType getSkipCertValidationType(
      Config config,
      UserIntent userIntent,
      Map<String, String> gflagsToAdd,
      Set<String> gflagsToRemove) {
    String configValue = config.getString(SKIP_CERT_VALIDATION);
    if (!configValue.isEmpty()) {
      try {
        return SkipCertValidationType.valueOf(configValue);
      } catch (Exception e) {
        log.error("Incorrect config value {} for {} ", configValue, SKIP_CERT_VALIDATION);
      }
    }
    if (gflagsToRemove.contains(VERIFY_SERVER_ENDPOINT_GFLAG)) {
      return SkipCertValidationType.NONE;
    }

    boolean skipHostValidation;
    if (gflagsToAdd.containsKey(VERIFY_SERVER_ENDPOINT_GFLAG)) {
      skipHostValidation = shouldSkipServerEndpointVerification(gflagsToAdd);
    } else {
      skipHostValidation =
          shouldSkipServerEndpointVerification(userIntent.masterGFlags)
              || shouldSkipServerEndpointVerification(userIntent.tserverGFlags);
    }
    return skipHostValidation ? SkipCertValidationType.HOSTNAME : SkipCertValidationType.NONE;
  }

  private static boolean shouldSkipServerEndpointVerification(Map<String, String> gflags) {
    return gflags.getOrDefault(VERIFY_SERVER_ENDPOINT_GFLAG, "true").equalsIgnoreCase("false");
  }

  private Map<String, String> getAnsibleEnvVars(UUID universeUUID) {
    Map<String, String> envVars = new HashMap<>();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    Config runtimeConfig = runtimeConfigFactory.forUniverse(universe);

    envVars.put("ANSIBLE_STRATEGY", runtimeConfig.getString("yb.ansible.strategy"));
    envVars.put(
        "ANSIBLE_TIMEOUT", Integer.toString(runtimeConfig.getInt("yb.ansible.conn_timeout_secs")));
    envVars.put(
        "ANSIBLE_VERBOSITY", Integer.toString(runtimeConfig.getInt("yb.ansible.verbosity")));
    if (runtimeConfig.getBoolean("yb.ansible.debug")) {
      envVars.put("ANSIBLE_DEBUG", "True");
    }
    if (runtimeConfig.getBoolean("yb.ansible.diff_always")) {
      envVars.put("ANSIBLE_DIFF_ALWAYS", "True");
    }
    envVars.put("ANSIBLE_LOCAL_TEMP", runtimeConfig.getString("yb.ansible.local_temp"));

    LOG.trace("ansible env vars {}", envVars);
    return envVars;
  }

  public ShellResponse detachedNodeCommand(
      NodeCommandType type, DetachedNodeTaskParams nodeTaskParam) {
    List<String> commandArgs = new ArrayList<>();
    if (type != NodeCommandType.Precheck) {
      throw new UnsupportedOperationException("Not supported " + type);
    }
    Provider provider = nodeTaskParam.getProvider();
    List<AccessKey> accessKeys = AccessKey.getAll(provider.uuid);
    if (accessKeys.isEmpty()) {
      throw new RuntimeException("No access keys for provider: " + provider.uuid);
    }
    AccessKey accessKey = accessKeys.get(0);
    AccessKey.KeyInfo keyInfo = accessKey.getKeyInfo();
    commandArgs.addAll(
        getAccessKeySpecificCommand(
            nodeTaskParam, type, keyInfo, Common.CloudType.onprem, accessKey.getKeyCode()));
    commandArgs.addAll(
        getCommunicationPortsParams(
            new UserIntent(), accessKey, new UniverseTaskParams.CommunicationPorts()));

    InstanceType instanceType = InstanceType.get(provider.uuid, nodeTaskParam.getInstanceType());
    commandArgs.add("--mount_points");
    commandArgs.add(instanceType.instanceTypeDetails.volumeDetailsList.get(0).mountPath);

    commandArgs.add(nodeTaskParam.getNodeName());

    NodeInstance nodeInstance = NodeInstance.getOrBadRequest(nodeTaskParam.getNodeUuid());
    JsonNode nodeDetails = Json.toJson(nodeInstance.getDetails());
    ((ObjectNode) nodeDetails).put("nodeName", DetachedNodeTaskParams.DEFAULT_NODE_NAME);

    List<String> cloudArgs = Arrays.asList("--node_metadata", Json.stringify(nodeDetails));

    return execCommand(
        nodeTaskParam.getRegion().uuid,
        null,
        null,
        type.toString().toLowerCase(),
        commandArgs,
        cloudArgs,
        Collections.emptyMap());
  }

  private Path addBootscript(
      String bootScript, List<String> commandArgs, NodeTaskParams nodeTaskParam) {
    Path bootScriptFile;
    commandArgs.add("--boot_script");

    // treat the contents as script body if it starts with a shebang line
    // otherwise consider the contents to be a path
    if (bootScript.startsWith("#!")) {
      try {
        bootScriptFile = Files.createTempFile(nodeTaskParam.nodeName, "-boot.sh");
        Files.write(bootScriptFile, bootScript.getBytes());
        Files.write(bootScriptFile, BOOT_SCRIPT_COMPLETE.getBytes(), StandardOpenOption.APPEND);

      } catch (IOException e) {
        LOG.error(e.getMessage(), e);
        throw new RuntimeException(e);
      }
    } else {
      try {
        bootScriptFile = Files.createTempFile(nodeTaskParam.nodeName, "-boot.sh");
        Files.write(bootScriptFile, Files.readAllBytes(Paths.get(bootScript)));
        Files.write(bootScriptFile, BOOT_SCRIPT_COMPLETE.getBytes(), StandardOpenOption.APPEND);

      } catch (IOException e) {
        LOG.error(e.getMessage(), e);
        throw new RuntimeException(e);
      }
    }
    commandArgs.add(bootScriptFile.toAbsolutePath().toString());
    commandArgs.add("--boot_script_token");
    commandArgs.add(BOOT_SCRIPT_TOKEN);
    return bootScriptFile;
  }

  private void addInstanceTags(
      Universe universe,
      UserIntent userIntent,
      NodeTaskParams nodeTaskParam,
      List<String> commandArgs) {
    if (Provider.InstanceTagsEnabledProviders.contains(userIntent.providerType)) {
      addInstanceTags(
          universe, userIntent.instanceTags, userIntent.providerType, nodeTaskParam, commandArgs);
    }
  }

  private void addInstanceTags(
      Universe universe,
      Map<String, String> instanceTags,
      Common.CloudType providerType,
      NodeTaskParams nodeTaskParam,
      List<String> commandArgs) {
    // Create an ordered shallow copy of the tags.
    Map<String, String> useTags = new TreeMap<>(instanceTags);
    filterInstanceTags(useTags, providerType);
    addAdditionalInstanceTags(universe, nodeTaskParam, useTags);
    if (!useTags.isEmpty()) {
      commandArgs.add("--instance_tags");
      commandArgs.add(Json.stringify(Json.toJson(useTags)));
    }
  }

  /** Remove tags that are restricted by provider. */
  private void filterInstanceTags(Map<String, String> instanceTags, Common.CloudType providerType) {
    if (providerType.equals(Common.CloudType.aws)) {
      // Do not allow users to overwrite the node name. Only AWS uses tags to set it.
      instanceTags.remove(UniverseDefinitionTaskBase.NODE_NAME_KEY);
    }
  }

  public ShellResponse nodeCommand(NodeCommandType type, NodeTaskParams nodeTaskParam) {
    Universe universe = Universe.getOrBadRequest(nodeTaskParam.universeUUID);
    populateNodeUuidFromUniverse(universe, nodeTaskParam);
    List<String> commandArgs = new ArrayList<>();
    UserIntent userIntent = getUserIntentFromParams(nodeTaskParam);
    Path bootScriptFile = null;
    Map<String, String> sensitiveData = new HashMap<>();
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
      case Create:
        {
          if (!(nodeTaskParam instanceof AnsibleCreateServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleCreateServer.Params");
          }
          Config config = this.runtimeConfigFactory.forProvider(nodeTaskParam.getProvider());
          AnsibleCreateServer.Params taskParam = (AnsibleCreateServer.Params) nodeTaskParam;
          Common.CloudType cloudType = userIntent.providerType;
          if (!cloudType.equals(Common.CloudType.onprem)) {
            commandArgs.add("--instance_type");
            commandArgs.add(taskParam.instanceType);
            commandArgs.add("--cloud_subnet");
            commandArgs.add(taskParam.subnetId);

            // Only create second NIC for cloud.
            if (config.getBoolean("yb.cloud.enabled") && taskParam.secondarySubnetId != null) {
              commandArgs.add("--cloud_subnet_secondary");
              commandArgs.add(taskParam.secondarySubnetId);
            }

            // Use case: cloud free tier instances.
            if (config.getBoolean("yb.cloud.enabled")) {
              // If low mem instance, configure small boot disk size.
              if (isLowMemInstanceType(taskParam.instanceType)) {
                String lowMemBootDiskSizeGB = "8";
                LOG.info(
                    "Detected low memory instance type. "
                        + "Setting up nodes using low boot disk size.");
                commandArgs.add("--boot_disk_size_gb");
                commandArgs.add(lowMemBootDiskSizeGB);
              }
            }

            String bootScript = config.getString(BOOT_SCRIPT_PATH);
            if (!bootScript.isEmpty()) {
              bootScriptFile = addBootscript(bootScript, commandArgs, nodeTaskParam);
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
            if (config.getBoolean("yb.cloud.enabled")
                && taskParam.assignPublicIP
                && taskParam.assignStaticPublicIP) {
              commandArgs.add("--assign_static_public_ip");
            }
          }
          addInstanceTags(universe, userIntent, nodeTaskParam, commandArgs);
          if (cloudType.equals(Common.CloudType.aws)) {
            if (taskParam.cmkArn != null) {
              commandArgs.add("--cmk_res_name");
              commandArgs.add(taskParam.cmkArn);
            }

            if (taskParam.ipArnString != null) {
              commandArgs.add("--iam_profile_arn");
              commandArgs.add(taskParam.ipArnString);
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
          if (type == NodeCommandType.Create) {
            commandArgs.add("--as_json");
          }
          break;
        }
      case Provision:
        {
          if (!(nodeTaskParam instanceof AnsibleSetupServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleSetupServer.Params");
          }
          AnsibleSetupServer.Params taskParam = (AnsibleSetupServer.Params) nodeTaskParam;
          Common.CloudType cloudType = userIntent.providerType;

          if (cloudType.equals(Common.CloudType.aws)) {
            // aws uses instance_type to determine device names for mounting
            commandArgs.add("--instance_type");
            commandArgs.add(taskParam.instanceType);
          }

          // gcp uses machine_image for ansible preprovision.yml
          if (cloudType.equals(Common.CloudType.gcp)) {
            String ybImage =
                Optional.ofNullable(taskParam.machineImage).orElse(taskParam.getRegion().ybImage);
            if (ybImage != null && !ybImage.isEmpty()) {
              commandArgs.add("--machine_image");
              commandArgs.add(ybImage);
            }
          }

          maybeAddVMImageCommandArgs(
              universe,
              cloudType,
              taskParam.vmUpgradeTaskType,
              !taskParam.ignoreUseCustomImageConfig,
              commandArgs);

          if (taskParam.isSystemdUpgrade) {
            // Cron to Systemd Upgrade
            commandArgs.add("--skip_preprovision");
            commandArgs.add("--tags");
            commandArgs.add("systemd_upgrade");
            commandArgs.add("--systemd_services");
          } else if (taskParam.useSystemd) {
            // Systemd for new universes
            commandArgs.add("--systemd_services");
          }

          if (taskParam.useTimeSync
              && (cloudType.equals(Common.CloudType.aws)
                  || cloudType.equals(Common.CloudType.gcp)
                  || cloudType.equals(Common.CloudType.azu))) {
            commandArgs.add("--use_chrony");
          }

          if (cloudType.equals(Common.CloudType.aws)) {
            if (!taskParam.remotePackagePath.isEmpty()) {
              commandArgs.add("--remote_package_path");
              commandArgs.add(taskParam.remotePackagePath);
            }
          }

          Config config = this.runtimeConfigFactory.forProvider(nodeTaskParam.getProvider());
          String bootScript = config.getString(BOOT_SCRIPT_PATH);
          if (!bootScript.isEmpty()) {
            bootScriptFile = addBootscript(bootScript, commandArgs, nodeTaskParam);
          }

          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
            DeviceInfo deviceInfo = nodeTaskParam.deviceInfo;
            // Need volume_type in GCP provision to determine correct device names for mounting
            if (deviceInfo.storageType != null && cloudType.equals(Common.CloudType.gcp)) {
              commandArgs.add("--volume_type");
              commandArgs.add(deviceInfo.storageType.toString().toLowerCase());
            }
          }

          String localPackagePath = getThirdpartyPackagePath();
          if (localPackagePath != null) {
            commandArgs.add("--local_package_path");
            commandArgs.add(localPackagePath);
          }

          commandArgs.add("--pg_max_mem_mb");
          commandArgs.add(
              Integer.toString(
                  runtimeConfigFactory.forUniverse(universe).getInt(POSTGRES_MAX_MEM_MB)));

          if (cloudType.equals(Common.CloudType.azu)) {
            NodeDetails node = universe.getNode(taskParam.nodeName);
            if (node != null && node.cloudInfo.lun_indexes.length > 0) {
              commandArgs.add("--lun_indexes");
              commandArgs.add(StringUtils.join(node.cloudInfo.lun_indexes, ","));
            }
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
          if (taskParam.isSystemdUpgrade) {
            // Cron to Systemd Upgrade
            commandArgs.add("--tags");
            commandArgs.add("systemd_upgrade");
            commandArgs.add("--systemd_services");
          } else if (taskParam.useSystemd) {
            // Systemd for new universes
            commandArgs.add("--systemd_services");
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          }
          sensitiveData.putAll(getReleaseSensitiveData(taskParam));
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
          if (taskParam.nodeUuid == null && Strings.isNullOrEmpty(taskParam.nodeIP)) {
            throw new IllegalArgumentException("At least one of node UUID or IP must be specified");
          }
          addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
          if (taskParam.nodeUuid != null) {
            commandArgs.add("--node_uuid");
            commandArgs.add(taskParam.nodeUuid.toString());
          }
          if (taskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(taskParam));
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          if (userIntent.assignStaticPublicIP) {
            commandArgs.add("--delete_static_public_ip");
          }
          break;
        }
      case Pause:
        {
          if (!(nodeTaskParam instanceof PauseServer.Params)) {
            throw new RuntimeException("NodeTaskParams is not PauseServer.Params");
          }
          PauseServer.Params taskParam = (PauseServer.Params) nodeTaskParam;
          addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
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
          addArguments(commandArgs, taskParam.nodeIP, taskParam.instanceType);
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
          // Systemd vs Cron Option
          if (taskParam.useSystemd) {
            commandArgs.add("--systemd_services");
          }
          if (taskParam.checkVolumesAttached) {
            UniverseDefinitionTaskParams.Cluster cluster =
                universe.getCluster(taskParam.placementUuid);
            if (cluster != null
                && cluster.userIntent.deviceInfo != null
                && cluster.userIntent.providerType != Common.CloudType.onprem) {
              commandArgs.add("--num_volumes");
              commandArgs.add(String.valueOf(cluster.userIntent.deviceInfo.numVolumes));
            }
          }
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
            Map<String, String> tags =
                taskParam.tags != null ? taskParam.tags : userIntent.instanceTags;
            if (MapUtils.isEmpty(tags) && taskParam.deleteTags.isEmpty()) {
              throw new RuntimeException("Invalid params: no tags to add or remove");
            }
            addInstanceTags(universe, tags, userIntent.providerType, nodeTaskParam, commandArgs);
            if (!taskParam.deleteTags.isEmpty()) {
              commandArgs.add("--remove_tags");
              commandArgs.add(taskParam.deleteTags);
            }
            if (userIntent.providerType.equals(Common.CloudType.azu)) {
              commandArgs.addAll(getDeviceArgs(taskParam));
              commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
            }
          } else {
            throw new IllegalArgumentException(
                "Tags are unsupported for " + userIntent.providerType);
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
      case Update_Mounted_Disks:
        {
          if (!(nodeTaskParam instanceof UpdateMountedDisks.Params)) {
            throw new RuntimeException("NodeTaskParams is not UpdateMountedDisksTask.Params");
          }
          UpdateMountedDisks.Params taskParam = (UpdateMountedDisks.Params) nodeTaskParam;
          commandArgs.add("--instance_type");
          commandArgs.add(taskParam.instanceType);
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.add("--volume_type");
            commandArgs.add(nodeTaskParam.deviceInfo.storageType.toString().toLowerCase());
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Change_Instance_Type:
        {
          if (!(nodeTaskParam instanceof ChangeInstanceType.Params)) {
            throw new RuntimeException("NodeTaskParams is not ResizeNode.Params");
          }
          ChangeInstanceType.Params taskParam = (ChangeInstanceType.Params) nodeTaskParam;
          commandArgs.add("--instance_type");
          commandArgs.add(taskParam.instanceType);
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case Transfer_XCluster_Certs:
        {
          if (!(nodeTaskParam instanceof TransferXClusterCerts.Params)) {
            throw new RuntimeException("NodeTaskParams is not TransferXClusterCerts.Params");
          }
          TransferXClusterCerts.Params taskParam = (TransferXClusterCerts.Params) nodeTaskParam;
          commandArgs.add("--action");
          commandArgs.add(taskParam.action.toString());
          if (taskParam.action == TransferXClusterCerts.Params.Action.COPY) {
            commandArgs.add("--root_cert_path");
            commandArgs.add(taskParam.rootCertPath.toString());
          }
          commandArgs.add("--replication_config_name");
          commandArgs.add(taskParam.replicationGroupName);
          if (taskParam.producerCertsDirOnTarget != null) {
            commandArgs.add("--producer_certs_dir");
            commandArgs.add(taskParam.producerCertsDirOnTarget.toString());
          }
          commandArgs.addAll(getAccessKeySpecificCommand(taskParam, type));
          break;
        }
      case CronCheck:
        {
          if (!(nodeTaskParam instanceof AnsibleConfigureServers.Params)) {
            throw new RuntimeException("NodeTaskParams is not AnsibleConfigureServers.Params");
          }
          commandArgs.addAll(getAccessKeySpecificCommand(nodeTaskParam, type));
          break;
        }
      case Precheck:
        {
          commandArgs.addAll(getAccessKeySpecificCommand(nodeTaskParam, type));
          if (nodeTaskParam.deviceInfo != null) {
            commandArgs.addAll(getDeviceArgs(nodeTaskParam));
          }
          AccessKey accessKey =
              AccessKey.getOrBadRequest(nodeTaskParam.getProvider().uuid, userIntent.accessKeyCode);
          commandArgs.addAll(
              getCommunicationPortsParams(userIntent, accessKey, nodeTaskParam.communicationPorts));

          boolean rootAndClientAreTheSame =
              nodeTaskParam.clientRootCA == null
                  || Objects.equals(nodeTaskParam.rootCA, nodeTaskParam.clientRootCA);
          appendCertPathsToCheck(
              commandArgs,
              nodeTaskParam.rootCA,
              false,
              rootAndClientAreTheSame && userIntent.enableNodeToNodeEncrypt);

          if (!rootAndClientAreTheSame) {
            appendCertPathsToCheck(commandArgs, nodeTaskParam.clientRootCA, true, false);
          }

          Config config = runtimeConfigFactory.forUniverse(universe);

          SkipCertValidationType skipType =
              getSkipCertValidationType(
                  config, userIntent, Collections.emptyMap(), Collections.emptySet());
          if (skipType != SkipCertValidationType.NONE) {
            commandArgs.add("--skip_cert_validation");
            commandArgs.add(skipType.name());
          }

          break;
        }
      case Delete_Root_Volumes:
        {
          addInstanceTags(universe, userIntent, nodeTaskParam, commandArgs);
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
          getCloudArgs(nodeTaskParam),
          getAnsibleEnvVars(nodeTaskParam.universeUUID),
          sensitiveData);
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

  private void appendCertPathsToCheck(
      List<String> commandArgs, UUID rootCA, boolean isClient, boolean appendClientPaths) {
    if (rootCA == null) {
      return;
    }
    CertificateInfo rootCert = CertificateInfo.get(rootCA);
    // checking only certs with CustomCertHostPath type, CustomServerCert is not used for onprem
    if (rootCert.certType != CertConfigType.CustomCertHostPath) {
      return;
    }
    String suffix = isClient ? "_client_to_server" : "";

    CertificateParams.CustomCertInfo customCertInfo = rootCert.getCustomCertPathParams();

    commandArgs.add(String.format("--root_cert_path%s", suffix));
    commandArgs.add(customCertInfo.rootCertPath);
    commandArgs.add(String.format("--server_cert_path%s", suffix));
    commandArgs.add(customCertInfo.nodeCertPath);
    commandArgs.add(String.format("--server_key_path%s", suffix));
    commandArgs.add(customCertInfo.nodeKeyPath);
    if (appendClientPaths
        && !StringUtils.isEmpty(customCertInfo.clientCertPath)
        && !StringUtils.isEmpty(customCertInfo.clientKeyPath)) {
      commandArgs.add("--client_cert_path");
      commandArgs.add(customCertInfo.clientCertPath);
      commandArgs.add("--client_key_path");
      commandArgs.add(customCertInfo.clientKeyPath);
    }
  }

  private Collection<String> getCommunicationPortsParams(
      UserIntent userIntent, AccessKey accessKey, UniverseTaskParams.CommunicationPorts ports) {
    List<String> result = new ArrayList<>();
    result.add("--master_http_port");
    result.add(Integer.toString(ports.masterHttpPort));
    result.add("--master_rpc_port");
    result.add(Integer.toString(ports.masterRpcPort));
    result.add("--tserver_http_port");
    result.add(Integer.toString(ports.tserverHttpPort));
    result.add("--tserver_rpc_port");
    result.add(Integer.toString(ports.tserverRpcPort));
    if (userIntent.enableYCQL) {
      result.add("--cql_proxy_http_port");
      result.add(Integer.toString(ports.yqlServerHttpPort));
      result.add("--cql_proxy_rpc_port");
      result.add(Integer.toString(ports.yqlServerRpcPort));
    }
    if (userIntent.enableYSQL) {
      result.add("--ysql_proxy_http_port");
      result.add(Integer.toString(ports.ysqlServerHttpPort));
      result.add("--ysql_proxy_rpc_port");
      result.add(Integer.toString(ports.ysqlServerRpcPort));
    }
    if (userIntent.enableYEDIS) {
      result.add("--redis_proxy_http_port");
      result.add(Integer.toString(ports.redisServerHttpPort));
      result.add("--redis_proxy_rpc_port");
      result.add(Integer.toString(ports.redisServerRpcPort));
    }
    if (accessKey.getKeyInfo().installNodeExporter) {
      result.add("--node_exporter_http_port");
      result.add(Integer.toString(ports.nodeExporterPort));
    }
    return result;
  }

  private void addArguments(List<String> commandArgs, String nodeIP, String instanceType) {
    commandArgs.add("--instance_type");
    commandArgs.add(instanceType);
    if (!Strings.isNullOrEmpty(nodeIP)) {
      commandArgs.add("--node_ip");
      commandArgs.add(nodeIP);
    }
  }

  private boolean isLowMemInstanceType(String instanceType) {
    List<String> lowMemInstanceTypePrefixes = ImmutableList.of("t2.", "t3.");
    String instanceTypePrefix = instanceType.split("\\.")[0] + ".";
    return lowMemInstanceTypePrefixes.contains(instanceTypePrefix);
  }

  // Set the nodeUuid in nodeTaskParam if it is not set.
  private void populateNodeUuidFromUniverse(Universe universe, NodeTaskParams nodeTaskParam) {
    if (nodeTaskParam.nodeUuid == null) {
      NodeDetails nodeDetails = universe.getNode(nodeTaskParam.nodeName);
      if (nodeDetails != null) {
        nodeTaskParam.nodeUuid = nodeDetails.nodeUuid;
      }
    }
    if (nodeTaskParam.nodeUuid == null) {
      UserIntent userIntent = getUserIntentFromParams(universe, nodeTaskParam);
      if (!Common.CloudType.onprem.equals(userIntent.providerType)) {
        // This is for backward compatibility where node UUID is not set in the Universe.
        nodeTaskParam.nodeUuid =
            Util.generateNodeUUID(universe.universeUUID, nodeTaskParam.nodeName);
      }
    }
  }

  private void addAdditionalInstanceTags(
      Universe universe, NodeTaskParams nodeTaskParam, Map<String, String> tags) {
    Customer customer = Customer.get(universe.customerId);
    tags.put("customer-uuid", customer.uuid.toString());
    tags.put("universe-uuid", universe.universeUUID.toString());
    tags.put("node-uuid", nodeTaskParam.nodeUuid.toString());
    UserIntent userIntent = getUserIntentFromParams(nodeTaskParam);
    if (userIntent.providerType.equals(Common.CloudType.gcp)) {
      // GCP does not allow special characters other than - and _
      // Special characters being replaced here
      // https://cloud.google.com/compute/docs/labeling-resources#requirements
      if (nodeTaskParam.creatingUser != null) {
        String email =
            SPECIAL_CHARACTERS_PATTERN
                .matcher(nodeTaskParam.creatingUser.getEmail())
                .replaceAll("_");
        tags.put("yb_user_email", email);
      }
      if (nodeTaskParam.platformUrl != null) {
        String url = SPECIAL_CHARACTERS_PATTERN.matcher(nodeTaskParam.platformUrl).replaceAll("_");
        tags.put("yb_yba_url", url);
      }

    } else {
      if (nodeTaskParam.creatingUser != null) {
        tags.put("yb_user_email", nodeTaskParam.creatingUser.getEmail());
      }
      if (nodeTaskParam.platformUrl != null) {
        tags.put("yb_yba_url", nodeTaskParam.platformUrl);
      }
    }
  }

  private Map<String, String> getReleaseSensitiveData(AnsibleConfigureServers.Params taskParam) {
    Map<String, String> data = new HashMap<>();
    if (taskParam.ybSoftwareVersion != null) {
      ReleaseManager.ReleaseMetadata releaseMetadata =
          releaseManager.getReleaseByVersion(taskParam.ybSoftwareVersion);
      if (releaseMetadata != null) {
        if (releaseMetadata.s3 != null) {
          data.put("--aws_access_key", releaseMetadata.s3.accessKeyId);
          data.put("--aws_secret_key", releaseMetadata.s3.secretAccessKey);
        } else if (releaseMetadata.gcs != null) {
          data.put("--gcs_credentials_json", releaseMetadata.gcs.credentialsJson);
        }
      }
    }
    return data;
  }

  private void maybeAddVMImageCommandArgs(
      Universe universe,
      Common.CloudType cloudType,
      VmUpgradeTaskType vmUpgradeTaskType,
      boolean useCustomImageByDefault,
      List<String> commandArgs) {
    if (!cloudType.equals(Common.CloudType.aws) && !cloudType.equals(Common.CloudType.gcp)) {
      return;
    }
    boolean skipTags = false;
    if (vmUpgradeTaskType == VmUpgradeTaskType.None
        && useCustomImageByDefault
        && universe.getConfig().getOrDefault(Universe.USE_CUSTOM_IMAGE, "false").equals("true")) {
      // Default image is custom image.
      skipTags = true;
    } else if (vmUpgradeTaskType == VmUpgradeTaskType.VmUpgradeWithCustomImages) {
      // This is set only if VMUpgrade is invoked.
      // This can also happen for platform only if yb.upgrade.vmImage is true.
      skipTags = true;
    }
    if (skipTags) {
      commandArgs.add("--skip_tags");
      commandArgs.add("yb-prebuilt-ami");
    }
  }
}
