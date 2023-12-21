// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.DELETE;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.BACKUP_SCRIPT;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.EMR_MULTIPLE;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.K8S_CERT_PATH;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.VM_CERT_DIR;
import static com.yugabyte.yw.common.backuprestore.BackupUtil.YB_CLOUD_COMMAND_TYPE;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.TableManagerParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@Singleton
@Slf4j
public class TableManagerYb extends DevopsBase {

  public enum CommandSubType {
    BACKUP(BACKUP_SCRIPT),
    BULK_IMPORT("bin/yb_bulk_load.py"),
    DELETE(BACKUP_SCRIPT);

    private String script;

    CommandSubType(String script) {
      this.script = script;
    }

    public String getScript() {
      return script;
    }
  }

  @Inject ReleaseManager releaseManager;
  @Inject StorageUtilFactory storageUtilFactory;

  public ShellResponse runCommand(CommandSubType subType, TableManagerParams taskParams)
      throws PlatformServiceException {
    Universe universe = Universe.getOrBadRequest(taskParams.getUniverseUUID());
    Architecture arch = universe.getUniverseDetails().arch;
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UniverseDefinitionTaskParams.UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.getProvider().getUuid());

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.getProvider().getUuid(), accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = CloudInfoInterface.fetchEnvVars(provider);
    Map<String, Map<String, String>> podAddrToConfig = new HashMap<>();
    Map<String, String> secondaryToPrimaryIP = new HashMap<>();
    Map<String, String> ipToSshKeyPath = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;

    if (region.getProviderCloudCode().equals(CloudType.kubernetes)) {
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        PlacementInfo pi = cluster.placementInfo;
        podAddrToConfig.putAll(
            KubernetesUtil.getKubernetesConfigPerPod(
                pi, universe.getUniverseDetails().getNodesInCluster(cluster.uuid)));
      }
    } else {
      // Populate the map so that we use the correct SSH Keys for the different
      // nodes in different clusters.
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        UserIntent clusterUserIntent = cluster.userIntent;
        Provider clusterProvider =
            Provider.getOrBadRequest(UUID.fromString(clusterUserIntent.provider));
        AccessKey accessKeyForCluster =
            AccessKey.getOrBadRequest(clusterProvider.getUuid(), clusterUserIntent.accessKeyCode);
        Collection<NodeDetails> nodesInCluster = universe.getNodesInCluster(cluster.uuid);
        for (NodeDetails nodeInCluster : nodesInCluster) {
          if (nodeInCluster.cloudInfo.private_ip != null
              && !nodeInCluster.cloudInfo.private_ip.equals("null")) {
            ipToSshKeyPath.put(
                nodeInCluster.cloudInfo.private_ip, accessKeyForCluster.getKeyInfo().privateKey);
          }
        }
      }
    }

    List<NodeDetails> tservers = universe.getTServers();
    // Verify if secondary IPs exist. If so, create map.
    boolean legacyNet =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("true");
    if (tservers.get(0).cloudInfo.secondary_private_ip != null
        && !tservers.get(0).cloudInfo.secondary_private_ip.equals("null")
        && !legacyNet) {
      secondaryToPrimaryIP =
          tservers.stream()
              .collect(
                  Collectors.toMap(
                      t -> t.cloudInfo.secondary_private_ip, t -> t.cloudInfo.private_ip));
    }

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(subType.getScript());
    commandArgs.add("--masters");
    commandArgs.add(universe.getMasterAddresses());

    BackupTableParams backupTableParams;
    Customer customer;
    CustomerConfig customerConfig;
    File backupKeysFile;

    switch (subType) {
      case BACKUP:
        backupTableParams = (BackupTableParams) taskParams;
        addAdditionalCommands(
            commandArgs, backupTableParams, userIntent, universe, secondaryToPrimaryIP);
        if (backupTableParams.tableUUIDList != null && !backupTableParams.tableUUIDList.isEmpty()) {
          for (int listIndex = 0; listIndex < backupTableParams.tableNameList.size(); listIndex++) {
            commandArgs.add("--table");
            commandArgs.add(backupTableParams.tableNameList.get(listIndex));
            commandArgs.add("--keyspace");
            commandArgs.add(backupTableParams.getKeyspace());
            commandArgs.add("--table_uuid");
            commandArgs.add(backupTableParams.tableUUIDList.get(listIndex).toString());
          }
        } else {
          commandArgs.add("--keyspace");
          if (backupTableParams.backupType == TableType.PGSQL_TABLE_TYPE) {
            commandArgs.add("ysql." + taskParams.getKeyspace());
          } else {
            commandArgs.add(taskParams.getKeyspace());
          }
          if (confGetter.getConfForScope(universe, UniverseConfKeys.pgBasedBackup)) {
            commandArgs.add("--pg_based_backup");
          }
        }
        commandArgs.add("--no_auto_name");
        customer = Customer.find.query().where().idEq(universe.getCustomerId()).findOne();
        customerConfig =
            CustomerConfig.get(customer.getUuid(), backupTableParams.storageConfigUUID);
        CustomerConfigStorageData configData =
            (CustomerConfigStorageData) customerConfig.getDataObject();
        Map<String, String> regionLocationMap =
            storageUtilFactory
                .getStorageUtil(customerConfig.getName())
                .getRegionLocationsMap(configData);
        if (MapUtils.isNotEmpty(regionLocationMap)) {
          regionLocationMap.forEach(
              (r, bL) -> {
                commandArgs.add("--region");
                commandArgs.add(r);
                commandArgs.add("--region_location");
                commandArgs.add(
                    BackupUtil.getExactRegionLocation(backupTableParams.storageLocation, bL));
              });
        }

        backupKeysFile =
            EncryptionAtRestUtil.getUniverseBackupKeysFile(backupTableParams.storageLocation);
        if (backupKeysFile.exists()) {
          commandArgs.add("--backup_keys_source");
          commandArgs.add(backupKeysFile.getAbsolutePath());
        }
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            podAddrToConfig,
            nodeToNodeTlsEnabled,
            ipToSshKeyPath,
            commandArgs,
            userIntent);
        commandArgs.add("create");
        extraVars.putAll(customerConfig.dataAsMap());

        log.info("Command to run: [" + String.join(" ", commandArgs) + "]");
        return shellProcessHandler.run(commandArgs, extraVars, backupTableParams.backupUuid);

      case BULK_IMPORT:
        commandArgs.add("--table");
        commandArgs.add(taskParams.getTableName());
        commandArgs.add("--keyspace");
        commandArgs.add(taskParams.getKeyspace());
        BulkImportParams bulkImportParams = (BulkImportParams) taskParams;
        ReleaseManager.ReleaseMetadata metadata =
            releaseManager.getReleaseByVersion(userIntent.ybSoftwareVersion);
        if (metadata == null) {
          throw new RuntimeException(
              "Unable to fetch yugabyte release for version: " + userIntent.ybSoftwareVersion);
        }
        String ybServerPackage;
        if (arch != null) {
          ybServerPackage = metadata.getFilePath(arch);
        } else {
          ybServerPackage = metadata.getFilePath(region);
        }
        if (bulkImportParams.instanceCount == 0) {
          bulkImportParams.instanceCount = userIntent.numNodes * EMR_MULTIPLE;
        }
        // TODO(bogdan): does this work?
        if (!region.getProviderCloudCode().equals(CloudType.kubernetes)) {
          commandArgs.add("--key_path");
          commandArgs.add(accessKey.getKeyInfo().privateKey);
        }
        commandArgs.add("--instance_count");
        commandArgs.add(Integer.toString(bulkImportParams.instanceCount));
        commandArgs.add("--universe");
        commandArgs.add(universe.getUniverseDetails().nodePrefix);
        commandArgs.add("--release");
        commandArgs.add(ybServerPackage);
        commandArgs.add("--s3bucket");
        commandArgs.add(bulkImportParams.s3Bucket);

        extraVars.put("AWS_DEFAULT_REGION", region.getCode());

        break;
      case DELETE:
        commandArgs.add("--ts_web_hosts_ports");
        commandArgs.add(universe.getTserverHTTPAddresses());
        backupTableParams = (BackupTableParams) taskParams;
        customer = Customer.find.query().where().idEq(universe.getCustomerId()).findOne();
        customerConfig =
            CustomerConfig.get(customer.getUuid(), backupTableParams.storageConfigUUID);
        log.info("Deleting backup at location {}", backupTableParams.storageLocation);
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            podAddrToConfig,
            nodeToNodeTlsEnabled,
            ipToSshKeyPath,
            commandArgs,
            userIntent);
        commandArgs.add("delete");
        extraVars.putAll(customerConfig.dataAsMap());
        break;
    }

    log.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraVars);
  }

  private String getCertsDir(Region region, Provider provider) {
    return region.getProviderCloudCode().equals(CloudType.kubernetes)
        ? K8S_CERT_PATH
        : provider.getYbHome() + VM_CERT_DIR;
  }

  private void addCommonCommandArgs(
      BackupTableParams backupTableParams,
      AccessKey accessKey,
      Region region,
      CustomerConfig customerConfig,
      Provider provider,
      Map<String, Map<String, String>> podAddrToConfig,
      boolean nodeToNodeTlsEnabled,
      Map<String, String> ipToSshKeyPath,
      List<String> commandArgs,
      UserIntent userIntent) {
    if (region.getProviderCloudCode().equals(CloudType.kubernetes)) {
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(podAddrToConfig)));
    } else {
      commandArgs.add("--ssh_port");
      commandArgs.add(provider.getDetails().sshPort.toString());
      commandArgs.add("--ssh_key_path");
      commandArgs.add(accessKey.getKeyInfo().privateKey);
      if (!ipToSshKeyPath.isEmpty()) {
        commandArgs.add("--ip_to_ssh_key_path");
        commandArgs.add(Json.stringify(Json.toJson(ipToSshKeyPath)));
      }
    }
    if (region.getProviderCloudCode().equals(CloudType.kubernetes) || userIntent.dedicatedNodes) {
      commandArgs.add("--useTserver");
    }
    Universe universe = Universe.getOrBadRequest(backupTableParams.getUniverseUUID());
    boolean useServerBroadcastAddress =
        confGetter.getConfForScope(universe, UniverseConfKeys.useServerBroadcastAddressForYbBackup);
    if (useServerBroadcastAddress) {
      commandArgs.add("--use_server_broadcast_address");
    }
    commandArgs.add("--backup_location");
    commandArgs.add(backupTableParams.storageLocation);
    commandArgs.add("--storage_type");
    commandArgs.add(customerConfig.getName().toLowerCase());
    if (customerConfig.getName().equalsIgnoreCase("nfs")) {
      commandArgs.add("--nfs_storage_path");
      commandArgs.add(
          ((CustomerConfigStorageNFSData) customerConfig.getDataObject()).backupLocation);
    }
    if (nodeToNodeTlsEnabled) {
      commandArgs.add("--certs_dir");
      commandArgs.add(getCertsDir(region, provider));
    }
    boolean verboseLogsEnabled =
        confGetter.getConfForScope(universe, UniverseConfKeys.backupLogVerbose);
    if (backupTableParams.enableVerboseLogs || verboseLogsEnabled) {
      commandArgs.add("--verbose");
    }
    if (backupTableParams.useTablespaces) {
      commandArgs.add("--use_tablespaces");
    }
    if (backupTableParams.disableChecksum) {
      commandArgs.add("--disable_checksums");
    }
    if (backupTableParams.disableMultipart) {
      commandArgs.add("--disable_multipart");
    }
    if (backupTableParams.disableParallelism) {
      commandArgs.add("--disable_parallelism");
    }
    if (confGetter.getGlobalConf(GlobalConfKeys.ssh2Enabled)) {
      commandArgs.add("--ssh2_enabled");
    }
    boolean enableSSE = confGetter.getConfForScope(universe, UniverseConfKeys.enableSSE);
    if (enableSSE) {
      commandArgs.add("--sse");
    }
    if (confGetter.getGlobalConf(GlobalConfKeys.disableXxHashChecksum)) {
      commandArgs.add("--disable_xxhash_checksum");
    }
  }

  private void addAdditionalCommands(
      List<String> commandArgs,
      BackupTableParams backupTableParams,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe,
      Map<String, String> secondaryToPrimaryIP) {
    commandArgs.add("--ts_web_hosts_ports");
    commandArgs.add(universe.getTserverHTTPAddresses());
    commandArgs.add("--parallelism");
    commandArgs.add(Integer.toString(backupTableParams.parallelism));
    if (userIntent.enableYSQLAuth
        || userIntent.tserverGFlags.getOrDefault("ysql_enable_auth", "false").equals("true")) {
      commandArgs.add("--ysql_enable_auth");
    }
    if (!secondaryToPrimaryIP.isEmpty()) {
      commandArgs.add("--ts_secondary_ip_map");
      commandArgs.add(Json.stringify(Json.toJson(secondaryToPrimaryIP)));
    }
    commandArgs.add("--ysql_port");
    commandArgs.add(
        Integer.toString(universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort));
  }

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public ShellResponse bulkImport(BulkImportParams taskParams) {
    return runCommand(BULK_IMPORT, taskParams);
  }

  public ShellResponse createBackup(BackupTableParams taskParams) {
    return runCommand(BACKUP, taskParams);
  }

  public ShellResponse deleteBackup(BackupTableParams taskParams) {
    return runCommand(DELETE, taskParams);
  }
}
