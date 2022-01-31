package com.yugabyte.yw.common;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.ActionType;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import play.libs.Json;

@Singleton
public class RestoreManagerYb extends DevopsBase {

  private static final int BACKUP_PREFIX_LENGTH = 8;
  private static final int TS_FMT_LENGTH = 19;
  private static final int UNIV_PREFIX_LENGTH = 6;
  private static final int UUID_LENGTH = 36;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  private static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  private static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  private static final String BACKUP_SCRIPT = "bin/yb_backup.py";

  @Inject CustomerConfigService customerConfigService;

  public ShellResponse runCommand(RestoreBackupParams restoreBackupParams) {
    Universe universe = Universe.getOrBadRequest(restoreBackupParams.universeUUID);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UniverseDefinitionTaskParams.UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.provider.uuid);

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = region.provider.getUnmaskedConfig();
    Map<String, String> namespaceToConfig = new HashMap<>();
    Map<String, String> secondaryToPrimaryIP = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;
    if (region.provider.code.equals("kubernetes")) {
      PlacementInfo pi = primaryCluster.placementInfo;
      namespaceToConfig =
          PlacementInfoUtil.getConfigPerNamespace(
              pi, universe.getUniverseDetails().nodePrefix, provider);
    }

    List<NodeDetails> tservers = universe.getTServers();
    // Verify if secondary IPs exist. If so, create map.
    if (tservers.get(0).cloudInfo.secondary_private_ip != null
        && !tservers.get(0).cloudInfo.secondary_private_ip.equals("null")) {
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
      if (backupStorageInfo.tableUUIDList != null && !backupStorageInfo.tableUUIDList.isEmpty()) {
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
        customerConfigService.getOrBadRequest(customer.uuid, backupStorageInfo.storageConfigUUID);
    File backupKeysFile =
        EncryptionAtRestUtil.getUniverseBackupKeysFile(backupStorageInfo.storageLocation);

    if (actionType.equals(ActionType.RESTORE_KEYS)) {
      if (!backupKeysFile.exists()
          && (backupKeysFile.getParentFile().exists() || backupKeysFile.getParentFile().mkdirs())) {
        commandArgs.add("--restore_keys_destination");
        commandArgs.add(backupKeysFile.getAbsolutePath());
      }
    }

    if (backupStorageInfo.tableUUIDList != null) {
      commandArgs.add("--table_uuid");
      commandArgs.add(backupStorageInfo.tableUUIDList.toString().replace("-", ""));
    }
    commandArgs.add("--no_auto_name");
    if (backupStorageInfo.sse) {
      commandArgs.add("--sse");
    }

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
      if (restoreBackupParams.newOwner != null) {
        commandArgs.add("--edit_ysql_dump_sed_reg_exp");
        commandArgs.add(
            String.format(
                "s|OWNER TO %s|OWNER TO %s|",
                restoreBackupParams.oldOwner, restoreBackupParams.newOwner));
      }
    }

    addCommonCommandArgs(
        restoreBackupParams,
        accessKey,
        region,
        customerConfig,
        provider,
        namespaceToConfig,
        nodeToNodeTlsEnabled,
        commandArgs);
    // Update env vars with customer config data after provider config to make sure the correct
    // credentials are used.
    extraVars.putAll(customerConfig.dataAsMap());

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
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
      RestoreBackupParams restoreBackupParams,
      AccessKey accessKey,
      Region region,
      CustomerConfig customerConfig,
      Provider provider,
      Map<String, String> namespaceToConfig,
      boolean nodeToNodeTlsEnabled,
      List<String> commandArgs) {

    BackupStorageInfo backupStorageInfo = restoreBackupParams.backupStorageInfoList.get(0);
    if (region.provider.code.equals("kubernetes")) {
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(namespaceToConfig)));
    } else {
      commandArgs.add("--ssh_port");
      commandArgs.add(accessKey.getKeyInfo().sshPort.toString());
      commandArgs.add("--ssh_key_path");
      commandArgs.add(accessKey.getKeyInfo().privateKey);
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
    if (restoreBackupParams.enableVerboseLogs) {
      commandArgs.add("--verbose");
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
