// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.DELETE;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.RESTORE;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.RESTORE_KEYS;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.TableManagerParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@Singleton
public class TableManagerYb extends DevopsBase {
  private static final int EMR_MULTIPLE = 8;
  private static final int BACKUP_PREFIX_LENGTH = 8;
  private static final int TS_FMT_LENGTH = 19;
  private static final int UNIV_PREFIX_LENGTH = 6;
  private static final int UUID_LENGTH = 36;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  private static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  private static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  private static final String BACKUP_SCRIPT = "bin/yb_backup.py";
  private static final String BACKUP_LOCATION = "BACKUP_LOCATION";

  public enum CommandSubType {
    BACKUP(BACKUP_SCRIPT),
    BULK_IMPORT("bin/yb_bulk_load.py"),
    RESTORE(BACKUP_SCRIPT),
    RESTORE_KEYS(BACKUP_SCRIPT),
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

  public ShellResponse runCommand(CommandSubType subType, TableManagerParams taskParams) {
    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UniverseDefinitionTaskParams.UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.provider.uuid);

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = region.provider.getUnmaskedConfig();
    Map<String, String> namespaceToConfig = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;

    if (region.provider.code.equals("kubernetes")) {
      PlacementInfo pi = primaryCluster.placementInfo;
      namespaceToConfig =
          PlacementInfoUtil.getConfigPerNamespace(
              pi, universe.getUniverseDetails().nodePrefix, provider);
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
        addAdditionalCommands(commandArgs, backupTableParams, userIntent, universe);
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
        }
        commandArgs.add("--no_auto_name");
        if (taskParams.sse) {
          commandArgs.add("--sse");
        }
        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
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
            namespaceToConfig,
            nodeToNodeTlsEnabled,
            commandArgs);
        commandArgs.add("create");
        extraVars.putAll(customerConfig.dataAsMap());

        LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
        return shellProcessHandler.run(commandArgs, extraVars, backupTableParams.backupUuid);

      case RESTORE:
        backupTableParams = (BackupTableParams) taskParams;
        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
        addAdditionalCommands(commandArgs, backupTableParams, userIntent, universe);
        if (backupTableParams.tableUUIDList != null && !backupTableParams.tableUUIDList.isEmpty()) {
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
        commandArgs.add("--no_auto_name");
        if (taskParams.sse) {
          commandArgs.add("--sse");
        }
        if (backupTableParams.restoreTimeStamp != null) {
          String backupLocation = customerConfig.data.get(BACKUP_LOCATION).asText();
          String restoreTimeStampMicroUnix =
              getValidatedRestoreTimeStampMicroUnix(
                  backupTableParams.restoreTimeStamp,
                  backupTableParams.storageLocation,
                  backupLocation);
          commandArgs.add("--restore_time");
          commandArgs.add(restoreTimeStampMicroUnix);
        }
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            namespaceToConfig,
            nodeToNodeTlsEnabled,
            commandArgs);
        commandArgs.add("restore");
        extraVars.putAll(customerConfig.dataAsMap());

        LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
        return shellProcessHandler.run(commandArgs, extraVars, backupTableParams.backupUuid);

      case RESTORE_KEYS:
        backupTableParams = (BackupTableParams) taskParams;
        addAdditionalCommands(commandArgs, backupTableParams, userIntent, universe);
        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
        backupKeysFile =
            EncryptionAtRestUtil.getUniverseBackupKeysFile(backupTableParams.storageLocation);
        if (!backupKeysFile.exists()
            && (backupKeysFile.getParentFile().exists()
                || backupKeysFile.getParentFile().mkdirs())) {
          commandArgs.add("--restore_keys_destination");
          commandArgs.add(backupKeysFile.getAbsolutePath());
        }
        commandArgs.add("--no_auto_name");
        if (taskParams.sse) {
          commandArgs.add("--sse");
        }
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            namespaceToConfig,
            nodeToNodeTlsEnabled,
            commandArgs);
        commandArgs.add("restore_keys");
        extraVars.putAll(customerConfig.dataAsMap());

        LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
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
        String ybServerPackage = metadata.filePath;
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
        commandArgs.add("--ts_web_hosts_ports");
        commandArgs.add(universe.getTserverHTTPAddresses());
        backupTableParams = (BackupTableParams) taskParams;
        customer = Customer.find.query().where().idEq(universe.customerId).findOne();
        customerConfig = CustomerConfig.get(customer.uuid, backupTableParams.storageConfigUUID);
        LOG.info("Deleting backup at location {}", backupTableParams.storageLocation);
        addCommonCommandArgs(
            backupTableParams,
            accessKey,
            region,
            customerConfig,
            provider,
            namespaceToConfig,
            nodeToNodeTlsEnabled,
            commandArgs);
        commandArgs.add("delete");
        extraVars.putAll(customerConfig.dataAsMap());
        break;
    }

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
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
      Map<String, String> namespaceToConfig,
      boolean nodeToNodeTlsEnabled,
      List<String> commandArgs) {
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
    commandArgs.add(backupTableParams.storageLocation);
    commandArgs.add("--storage_type");
    commandArgs.add(customerConfig.name.toLowerCase());
    if (customerConfig.name.toLowerCase().equals("nfs")) {
      commandArgs.add("--nfs_storage_path");
      commandArgs.add(customerConfig.getData().get(BACKUP_LOCATION).asText());
    }
    if (nodeToNodeTlsEnabled) {
      commandArgs.add("--certs_dir");
      commandArgs.add(getCertsDir(region, provider));
    }
    if (backupTableParams.enableVerboseLogs) {
      commandArgs.add("--verbose");
    }
  }

  private void addAdditionalCommands(
      List<String> commandArgs,
      BackupTableParams backupTableParams,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe) {
    commandArgs.add("--ts_web_hosts_ports");
    commandArgs.add(universe.getTserverHTTPAddresses());
    commandArgs.add("--parallelism");
    commandArgs.add(Integer.toString(backupTableParams.parallelism));
    if (userIntent.enableYSQLAuth
        || userIntent.tserverGFlags.getOrDefault("ysql_enable_auth", "false").equals("true")) {
      commandArgs.add("--ysql_enable_auth");
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

  public ShellResponse createRestore(BackupTableParams taskParams) {
    return runCommand(RESTORE, taskParams);
  }

  public ShellResponse createRestoreKeys(BackupTableParams taskParams) {
    return runCommand(RESTORE_KEYS, taskParams);
  }
}
