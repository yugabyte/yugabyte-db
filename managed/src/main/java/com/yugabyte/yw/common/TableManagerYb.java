// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.BULK_IMPORT;
import static com.yugabyte.yw.common.TableManagerYb.CommandSubType.DELETE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;

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
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@Singleton
public class TableManagerYb extends DevopsBase {
  private static final int EMR_MULTIPLE = 8;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  private static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  private static final String VM_CERT_DIR = "/yugabyte-tls-config/";
  private static final String BACKUP_SCRIPT = "bin/yb_backup.py";

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
      commandArgs.add(customerConfig.getData().get(BACKUP_LOCATION_FIELDNAME).asText());
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
