// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.BackupUtil.BACKUP_PREFIX_LENGTH;
import static com.yugabyte.yw.common.BackupUtil.BACKUP_SCRIPT;
import static com.yugabyte.yw.common.BackupUtil.EMR_MULTIPLE;
import static com.yugabyte.yw.common.BackupUtil.K8S_CERT_PATH;
import static com.yugabyte.yw.common.BackupUtil.REGION_LOCATIONS;
import static com.yugabyte.yw.common.BackupUtil.REGION_NAME;
import static com.yugabyte.yw.common.BackupUtil.TS_FMT_LENGTH;
import static com.yugabyte.yw.common.BackupUtil.UNIV_PREFIX_LENGTH;
import static com.yugabyte.yw.common.BackupUtil.UUID_LENGTH;
import static com.yugabyte.yw.common.BackupUtil.VM_CERT_DIR;
import static com.yugabyte.yw.common.BackupUtil.YB_CLOUD_COMMAND_TYPE;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.TableManager.CommandSubType.DELETE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.REGION_LOCATION_FIELDNAME;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.TableManagerParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang.StringUtils;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@Singleton
@Slf4j
public class TableManager extends DevopsBase {

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
  @Inject BackupUtil backupUtil;

  public ShellResponse runCommand(CommandSubType subType, TableManagerParams taskParams) {
    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.provider.uuid);

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = region.provider.getUnmaskedConfig();
    Map<String, String> podFQDNToConfig = new HashMap<>();
    Map<String, String> secondaryToPrimaryIP = new HashMap<>();
    Map<String, String> ipToSshKeyPath = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;

    if (region.provider.code.equals("kubernetes")) {
      PlacementInfo pi = primaryCluster.placementInfo;
      podFQDNToConfig =
          PlacementInfoUtil.getKubernetesConfigPerPod(
              pi, universe.getUniverseDetails().getNodesInCluster(primaryCluster.uuid));
    } else {
      // Populate the map so that we use the correct SSH Keys for the different
      // nodes in different clusters.
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        UserIntent clusterUserIntent = cluster.userIntent;
        Provider clusterProvider =
            Provider.getOrBadRequest(UUID.fromString(clusterUserIntent.provider));
        AccessKey accessKeyForCluster =
            AccessKey.getOrBadRequest(clusterProvider.uuid, clusterUserIntent.accessKeyCode);
        Collection<NodeDetails> nodesInCluster = universe.getNodesInCluster(cluster.uuid);
        for (NodeDetails nodeInCluster : nodesInCluster) {
          ipToSshKeyPath.put(
              nodeInCluster.cloudInfo.private_ip, accessKeyForCluster.getKeyInfo().privateKey);
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
          tservers
              .stream()
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

    switch (subType) {
      case BACKUP:
        backupTableParams = (BackupTableParams) taskParams;

        commandArgs.add("--ts_web_hosts_ports");
        commandArgs.add(universe.getTserverHTTPAddresses());
        if (!secondaryToPrimaryIP.isEmpty()) {
          commandArgs.add("--ts_secondary_ip_map");
          commandArgs.add(Json.stringify(Json.toJson(secondaryToPrimaryIP)));
        }
        commandArgs.add("--parallelism");
        commandArgs.add(Integer.toString(backupTableParams.parallelism));
        if (userIntent.isYSQLAuthEnabled()) {
          commandArgs.add("--ysql_enable_auth");
        }
        commandArgs.add("--ysql_port");
        commandArgs.add(
            Integer.toString(universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort));

        if (backupTableParams.actionType == BackupTableParams.ActionType.CREATE) {
          if (backupTableParams.tableUUIDList != null
              && !backupTableParams.tableUUIDList.isEmpty()) {
            for (int listIndex = 0;
                listIndex < backupTableParams.tableNameList.size();
                listIndex++) {
              commandArgs.add("--table");
              commandArgs.add(backupTableParams.tableNameList.get(listIndex));
              commandArgs.add("--keyspace");
              commandArgs.add(backupTableParams.getKeyspace());
              commandArgs.add("--table_uuid");
              commandArgs.add(backupTableParams.tableUUIDList.get(listIndex).toString());
            }
          } else {
            if (backupTableParams.getTableName() != null) {
              commandArgs.add("--table");
              commandArgs.add(taskParams.getTableName());
            }
            commandArgs.add("--keyspace");
            if (backupTableParams.backupType == TableType.PGSQL_TABLE_TYPE) {
              commandArgs.add("ysql." + taskParams.getKeyspace());
            } else {
              commandArgs.add(taskParams.getKeyspace());
            }
            if (runtimeConfigFactory.forUniverse(universe).getBoolean("yb.backup.pg_based")) {
              commandArgs.add("--pg_based_backup");
            }
          }
        } else if (backupTableParams.actionType == BackupTableParams.ActionType.RESTORE) {
          if (backupTableParams.tableUUIDList != null
              && !backupTableParams.tableUUIDList.isEmpty()) {
            for (String tableName : backupTableParams.tableNameList) {
              commandArgs.add("--table");
              commandArgs.add(tableName);
            }
          } else if (backupTableParams.getTableName() != null) {
            commandArgs.add("--table");
            commandArgs.add(taskParams.getTableName());
          }
          if (backupTableParams.getKeyspace() != null) {
            commandArgs.add("--keyspace");
            commandArgs.add(backupTableParams.getKeyspace());
          }
        }

        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
        File backupKeysFile =
            EncryptionAtRestUtil.getUniverseBackupKeysFile(backupTableParams.storageLocation);

        if (backupTableParams.actionType.equals(BackupTableParams.ActionType.CREATE)) {
          if (backupKeysFile.exists()) {
            commandArgs.add("--backup_keys_source");
            commandArgs.add(backupKeysFile.getAbsolutePath());
          }
        } else if (backupTableParams.actionType.equals(BackupTableParams.ActionType.RESTORE_KEYS)) {
          if (!backupKeysFile.exists()
              && (backupKeysFile.getParentFile().exists()
                  || backupKeysFile.getParentFile().mkdirs())) {
            commandArgs.add("--restore_keys_destination");
            commandArgs.add(backupKeysFile.getAbsolutePath());
          }
        }
        if (taskParams.tableUUID != null) {
          commandArgs.add("--table_uuid");
          commandArgs.add(taskParams.tableUUID.toString().replace("-", ""));
        }
        commandArgs.add("--no_auto_name");
        if (taskParams.sse) {
          commandArgs.add("--sse");
        }
        if (backupTableParams.actionType == BackupTableParams.ActionType.RESTORE) {
          if (backupTableParams.restoreTimeStamp != null) {
            String backupLocation = customerConfig.data.get(BACKUP_LOCATION_FIELDNAME).asText();
            String restoreTimeStampMicroUnix =
                getValidatedRestoreTimeStampMicroUnix(
                    backupTableParams.restoreTimeStamp,
                    backupTableParams.storageLocation,
                    backupLocation);
            commandArgs.add("--restore_time");
            commandArgs.add(restoreTimeStampMicroUnix);
          }
          if (backupTableParams.newOwner != null) {
            commandArgs.add("--edit_ysql_dump_sed_reg_exp");
            commandArgs.add(
                String.format(
                    "s|OWNER TO %s|OWNER TO %s|",
                    backupTableParams.oldOwner, backupTableParams.newOwner));
          }
        }
        if (backupTableParams.actionType.equals(BackupTableParams.ActionType.CREATE)
            && !customerConfig.name.toLowerCase().equals("nfs")) {
          // For non-nfs configurations we are adding region configurations.
          JsonNode regions = customerConfig.getData().get(REGION_LOCATIONS);
          if ((regions != null) && regions.isArray()) {
            for (JsonNode regionSettings : regions) {
              JsonNode regionName = regionSettings.get(REGION_NAME);
              JsonNode regionLocation = regionSettings.get(REGION_LOCATION_FIELDNAME);
              if ((regionName != null)
                  && !StringUtils.isEmpty(regionName.asText())
                  && (regionLocation != null)
                  && !StringUtils.isEmpty(regionLocation.asText())) {
                commandArgs.add("--region");
                commandArgs.add(regionName.asText().toLowerCase());
                commandArgs.add("--region_location");
                commandArgs.add(
                    BackupUtil.getExactRegionLocation(backupTableParams, regionLocation.asText()));
              }
            }
          }
        }
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            podFQDNToConfig,
            nodeToNodeTlsEnabled,
            ipToSshKeyPath,
            commandArgs);
        // Update env vars with customer config data after provider config to make sure the correct
        // credentials are used.
        extraVars.putAll(customerConfig.dataAsMap());

        log.info("Command to run: [" + String.join(" ", commandArgs) + "]");
        return shellProcessHandler.run(commandArgs, extraVars, backupTableParams.backupUuid);
        // TODO: Add support for TLS connections for bulk-loading.
        // Tracked by issue: https://github.com/YugaByte/yugabyte-db/issues/1864
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
        String ybServerPackage = metadata.getFilePath(region);
        if (bulkImportParams.instanceCount == 0) {
          bulkImportParams.instanceCount = userIntent.numNodes * EMR_MULTIPLE;
        }
        // TODO(bogdan): does this work?
        if (!region.provider.code.equals("kubernetes")) {
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

        extraVars.put("AWS_DEFAULT_REGION", region.code);

        break;
      case DELETE:
        backupTableParams = (BackupTableParams) taskParams;
        commandArgs.add("--ts_web_hosts_ports");
        commandArgs.add(universe.getTserverHTTPAddresses());
        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
        log.info("Deleting backup at location {}", backupTableParams.storageLocation);
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            podFQDNToConfig,
            nodeToNodeTlsEnabled,
            ipToSshKeyPath,
            commandArgs);
        extraVars.putAll(customerConfig.dataAsMap());
        break;
    }

    log.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraVars);
  }

  private String getCertsDir(Region region, Provider provider) {
    return region.provider.code.equals("kubernetes")
        ? K8S_CERT_PATH
        : provider.getYbHome() + VM_CERT_DIR;
  }

  private String getValidatedRestoreTimeStampMicroUnix(
      String restoreTimeStamp, String storageLocation, String storageLocationPrefix) {
    try {
      long restoreTimeMicroUnix =
          Util.microUnixTimeFromDateString(restoreTimeStamp, "yyyy-MM-dd HH:mm:ss");

      // we will remove the backupLocation from the storageLocation, so after that we are left with
      // /univ-<univ_uuid>/backup-<timestamp>-<something_to_disambiguate_from_yugaware>
      // /table-keyspace.table_name.table_uuid
      // After receiving the storageLocation in above format we will be extracting the tsformat
      // timestamp of length 19 by removing "/univ-", "<univ-UUID>", "/backup-".
      String backupCreationTime =
          storageLocation
              .replaceFirst(storageLocationPrefix, "")
              .substring(
                  UNIV_PREFIX_LENGTH + UUID_LENGTH + BACKUP_PREFIX_LENGTH,
                  UNIV_PREFIX_LENGTH + UUID_LENGTH + BACKUP_PREFIX_LENGTH + TS_FMT_LENGTH);
      long backupCreationTimeMicroUnix =
          Util.microUnixTimeFromDateString(backupCreationTime, "yyyy-MM-dd'T'HH:mm:ss");

      // Currently, we cannot validate input restoreTimeStamp with the desired backup's restore time
      // lower_bound limit.
      // As we require "timestamp_history_retention_interval_sec" flag value which has to be
      // captured during the backup creation and also to be stored in backup metadata.
      // Even after that we still have to figure out a way to extract the value in
      // Platform as we only have storageLocation as parameter form user.
      if (restoreTimeMicroUnix > backupCreationTimeMicroUnix) {
        throw new RuntimeException(
            "Restore TimeStamp is not within backup creation TimeStamp boundaries.");
      }
      return Long.toString(restoreTimeMicroUnix);
    } catch (ParseException e) {
      throw new RuntimeException(
          "Invalid restore timeStamp format, Please provide it in yyyy-MM-dd HH:mm:ss format");
    }
  }

  private void addCommonCommandArgs(
      BackupTableParams backupTableParams,
      AccessKey accessKey,
      Region region,
      CustomerConfig customerConfig,
      Provider provider,
      Map<String, String> podFQDNToConfig,
      boolean nodeToNodeTlsEnabled,
      Map<String, String> ipToSshKeyPath,
      List<String> commandArgs) {
    if (region.provider.code.equals("kubernetes")) {
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(podFQDNToConfig)));
    } else {
      commandArgs.add("--ssh_port");
      commandArgs.add(accessKey.getKeyInfo().sshPort.toString());
      commandArgs.add("--ssh_key_path");
      commandArgs.add(accessKey.getKeyInfo().privateKey);
      if (!ipToSshKeyPath.isEmpty()) {
        commandArgs.add("--ip_to_ssh_key_path");
        commandArgs.add(Json.stringify(Json.toJson(ipToSshKeyPath)));
      }
    }
    commandArgs.add("--backup_location");
    commandArgs.add(backupTableParams.storageLocation);
    commandArgs.add("--storage_type");
    commandArgs.add(customerConfig.name.toLowerCase());
    if (customerConfig.name.toLowerCase().equals("nfs")) {
      commandArgs.add("--nfs_storage_path");
      commandArgs.add(customerConfig.getData().get(BACKUP_LOCATION_FIELDNAME).asText());
    }

    if (nodeToNodeTlsEnabled) {
      commandArgs.add("--certs_dir");
      commandArgs.add(getCertsDir(region, provider));
    }
    commandArgs.add(backupTableParams.actionType.name().toLowerCase());
    if (backupTableParams.enableVerboseLogs) {
      commandArgs.add("--verbose");
    }
    if (backupTableParams.useTablespaces) {
      commandArgs.add("--use_tablespaces");
    }
    if (backupTableParams.disableChecksum) {
      commandArgs.add("--disable_checksums");
    }
    if (backupTableParams.disableParallelism) {
      commandArgs.add("--disable_parallelism");
    }
    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ssh2_enabled")) {
      commandArgs.add("--ssh2_enabled");
    }
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
