package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.ActionType;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
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

import org.apache.commons.lang3.StringUtils;

import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Singleton
@Slf4j
public class RestoreManagerYb extends DevopsBase {

  private static final int BACKUP_PREFIX_LENGTH = 8;
  private static final int TS_FMT_LENGTH = 19;
  private static final int UNIV_PREFIX_LENGTH = 6;
  private static final int UUID_LENGTH = 36;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  private static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  private static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  private static final String BACKUP_SCRIPT = "bin/yb_backup.py";

  public ShellResponse runCommand(RestoreBackupParams restoreBackupParams) {
    Universe universe = Universe.getOrBadRequest(restoreBackupParams.universeUUID);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.provider.uuid);

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = region.provider.getUnmaskedConfig();
    Map<String, Map<String, String>> podAddrToConfig = new HashMap<>();
    Map<String, String> secondaryToPrimaryIP = new HashMap<>();
    Map<String, String> ipToSshKeyPath = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;
    if (region.provider.code.equals("kubernetes")) {
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
            AccessKey.getOrBadRequest(clusterProvider.uuid, clusterUserIntent.accessKeyCode);
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
          tservers
              .stream()
              .collect(
                  Collectors.toMap(
                      t -> t.cloudInfo.secondary_private_ip, t -> t.cloudInfo.private_ip));
    }

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(BACKUP_SCRIPT);
    commandArgs.add("--masters");
    commandArgs.add(universe.getMasterAddresses());

    commandArgs.add("--ts_web_hosts_ports");
    commandArgs.add(universe.getTserverHTTPAddresses());

    if (!secondaryToPrimaryIP.isEmpty()) {
      commandArgs.add("--ts_secondary_ip_map");
      commandArgs.add(Json.stringify(Json.toJson(secondaryToPrimaryIP)));
    }

    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ssh2_enabled")) {
      commandArgs.add("--ssh2_enabled");
    }

    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.backup.disable_xxhash_checksum")) {
      commandArgs.add("--disable_xxhash_checksum");
    }

    commandArgs.add("--parallelism");
    commandArgs.add(Integer.toString(restoreBackupParams.parallelism));
    if (userIntent.enableYSQLAuth
        || userIntent.tserverGFlags.getOrDefault("ysql_enable_auth", "false").equals("true")) {
      commandArgs.add("--ysql_enable_auth");
    }
    commandArgs.add("--ysql_port");
    commandArgs.add(
        Integer.toString(universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort));

    BackupStorageInfo backupStorageInfo = restoreBackupParams.backupStorageInfoList.get(0);
    ActionType actionType = restoreBackupParams.actionType;
    if (actionType.equals(ActionType.RESTORE)) {
      if (backupStorageInfo.tableNameList != null) {
        for (String tableName : backupStorageInfo.tableNameList) {
          commandArgs.add("--table");
          commandArgs.add(tableName);
        }
      }
      if (backupStorageInfo.keyspace != null) {
        commandArgs.add("--keyspace");
        commandArgs.add(backupStorageInfo.keyspace);
      }
    }

    Customer customer = Customer.get(universe.customerId);
    CustomerConfig customerConfig =
        CustomerConfig.get(customer.uuid, restoreBackupParams.storageConfigUUID);
    File backupKeysFile =
        EncryptionAtRestUtil.getUniverseBackupKeysFile(backupStorageInfo.storageLocation);

    if (actionType.equals(ActionType.RESTORE_KEYS)) {
      if (!backupKeysFile.exists()
          && (backupKeysFile.getParentFile().exists() || backupKeysFile.getParentFile().mkdirs())) {
        commandArgs.add("--restore_keys_destination");
        commandArgs.add(backupKeysFile.getAbsolutePath());
      }
    }

    commandArgs.add("--no_auto_name");
    if (actionType.equals(ActionType.RESTORE)) {
      if (restoreBackupParams.restoreTimeStamp != null) {
        String backupLocation = customerConfig.data.get(BACKUP_LOCATION_FIELDNAME).asText();
        String restoreTimeStampMicroUnix =
            getValidatedRestoreTimeStampMicroUnix(
                restoreBackupParams.restoreTimeStamp,
                backupStorageInfo.storageLocation,
                backupLocation);
        commandArgs.add("--restore_time");
        commandArgs.add(restoreTimeStampMicroUnix);
      }
      if (StringUtils.isNotBlank(backupStorageInfo.newOwner)) {
        commandArgs.add("--edit_ysql_dump_sed_reg_exp");
        commandArgs.add(
            String.format(
                "s|OWNER TO %s|OWNER TO %s|",
                backupStorageInfo.oldOwner, backupStorageInfo.newOwner));
      }
    }

    addCommonCommandArgs(
        universe,
        restoreBackupParams,
        accessKey,
        region,
        customerConfig,
        provider,
        podAddrToConfig,
        nodeToNodeTlsEnabled,
        ipToSshKeyPath,
        commandArgs,
        userIntent);
    // Update env vars with customer config data after provider config to make sure the correct
    // credentials are used.
    extraVars.putAll(customerConfig.dataAsMap());

    log.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraVars);
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
      Universe universe,
      RestoreBackupParams restoreBackupParams,
      AccessKey accessKey,
      Region region,
      CustomerConfig customerConfig,
      Provider provider,
      Map<String, Map<String, String>> podAddrToConfig,
      boolean nodeToNodeTlsEnabled,
      Map<String, String> ipToSshKeyPath,
      List<String> commandArgs,
      UserIntent userIntent) {

    BackupStorageInfo backupStorageInfo = restoreBackupParams.backupStorageInfoList.get(0);
    if (region.provider.code.equals("kubernetes") || userIntent.dedicatedNodes) {
      commandArgs.add("--useTserver");
    }
    if (region.provider.code.equals("kubernetes")) {
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(podAddrToConfig)));
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
    commandArgs.add(backupStorageInfo.storageLocation);
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
    commandArgs.add(restoreBackupParams.actionType.name().toLowerCase());
    boolean verboseLogsEnabled =
        runtimeConfigFactory.forUniverse(universe).getBoolean("yb.backup.log.verbose");
    if (restoreBackupParams.enableVerboseLogs || verboseLogsEnabled) {
      commandArgs.add("--verbose");
    }
    if (runtimeConfigFactory.forUniverse(universe).getBoolean("yb.backup.enable_sse")) {
      commandArgs.add("--sse");
    }
    if (restoreBackupParams.useTablespaces) {
      commandArgs.add("--use_tablespaces");
    }
    if (restoreBackupParams.disableChecksum) {
      commandArgs.add("--disable_checksums");
    }
    if (restoreBackupParams.disableMultipart) {
      commandArgs.add("--disable_multipart");
    }
  }

  private String getCertsDir(Region region, Provider provider) {
    return region.provider.code.equals("kubernetes")
        ? K8S_CERT_PATH
        : provider.getYbHome() + VM_CERT_DIR;
  }

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }
}
