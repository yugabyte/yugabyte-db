// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import com.yugabyte.yw.common.PlacementInfoUtil;

import play.libs.Json;

import static com.yugabyte.yw.common.TableManager.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BULK_IMPORT;

@Singleton
public class TableManager extends DevopsBase {
  private static final int EMR_MULTIPLE = 8;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  private static final String K8S_CERT_PATH = "/opt/certs/yugabyte/";
  private static final String VM_CERT_DIR = "/yugabyte-tls-config/";

  public enum CommandSubType {
    BACKUP,

    BULK_IMPORT;

    public String getScript() {
      switch (this) {
        case BACKUP:
          return "bin/yb_backup.py";
        case BULK_IMPORT:
          return "bin/yb_bulk_load.py";
      }
      return null;
    }
  }

  @Inject
  ReleaseManager releaseManager;

  public ShellProcessHandler.ShellResponse runCommand(CommandSubType subType,
                                                      TableManagerParams taskParams) {
    Universe universe = Universe.get(taskParams.universeUUID);
    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
    Region region = Region.get(primaryCluster.userIntent.regionList.get(0));
    UniverseDefinitionTaskParams.UserIntent userIntent = primaryCluster.userIntent;
    Provider provider = Provider.get(region.provider.uuid);

    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = new HashMap<>();
    Map<String, String> namespaceToConfig = new HashMap<>();

    boolean nodeToNodeTlsEnabled = userIntent.enableNodeToNodeEncrypt;
    String yb_home_dir = provider.getYbHome();
    String certsDir = yb_home_dir + VM_CERT_DIR;

    if (region.provider.code.equals("kubernetes")) {
      PlacementInfo pi = primaryCluster.placementInfo;
      namespaceToConfig = PlacementInfoUtil.getConfigPerNamespace(pi,
          universe.getUniverseDetails().nodePrefix, provider);
    }

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(subType.getScript());
    commandArgs.add("--masters");
    commandArgs.add(universe.getMasterAddresses());
    commandArgs.add("--table");
    commandArgs.add(taskParams.tableName);
    commandArgs.add("--keyspace");
    commandArgs.add(taskParams.keyspace);

    switch (subType) {
      case BACKUP:
        BackupTableParams backupTableParams = (BackupTableParams) taskParams;
        Customer customer = Customer.find.where().idEq(universe.customerId).findUnique();
        CustomerConfig customerConfig = CustomerConfig.get(customer.uuid,
                                                           backupTableParams.storageConfigUUID);

        if (region.provider.code.equals("kubernetes")) {
            commandArgs.add("--k8s_config");
            commandArgs.add(Json.stringify(Json.toJson(namespaceToConfig)));
            certsDir = K8S_CERT_PATH;
        } else {
          if (accessKey.getKeyInfo().sshUser != null && !accessKey.getKeyInfo().sshUser.isEmpty()) {
            commandArgs.add("--ssh_user");
            commandArgs.add(accessKey.getKeyInfo().sshUser);
          }
          commandArgs.add("--ssh_port");
          commandArgs.add(accessKey.getKeyInfo().sshPort.toString());
          commandArgs.add("--ssh_key_path");
          commandArgs.add(accessKey.getKeyInfo().privateKey);
        }
        commandArgs.add("--backup_location");
        commandArgs.add(backupTableParams.storageLocation);
        commandArgs.add("--storage_type");
        commandArgs.add(customerConfig.name.toLowerCase());
        if (taskParams.tableUUID != null) {
          commandArgs.add("--table_uuid");
          commandArgs.add(taskParams.tableUUID.toString().replace("-", ""));
        }
        commandArgs.add("--no_auto_name");
        if (nodeToNodeTlsEnabled) {
          commandArgs.add("--certs_dir");
          commandArgs.add(certsDir);
        }
        commandArgs.add(backupTableParams.actionType.name().toLowerCase());
        if (backupTableParams.enableVerboseLogs) {
          commandArgs.add("--verbose");
        }
        if (taskParams.sse) {
          commandArgs.add("--sse");
        }
        extraVars = customerConfig.dataAsMap();
        if (region.provider.code.equals("kubernetes")) {
          extraVars.putAll(region.provider.getConfig());
        }

        break;
      // TODO: Add support for TLS connections for bulk-loading.
      // Tracked by issue: https://github.com/YugaByte/yugabyte-db/issues/1864
      case BULK_IMPORT:
        BulkImportParams bulkImportParams = (BulkImportParams) taskParams;
        ReleaseManager.ReleaseMetadata metadata =
            releaseManager.getReleaseByVersion(userIntent.ybSoftwareVersion);
        if (metadata == null) {
          throw new RuntimeException("Unable to fetch yugabyte release for version: " +
              userIntent.ybSoftwareVersion);
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

        extraVars = region.provider.getConfig();
        extraVars.put("AWS_DEFAULT_REGION", region.code);

        break;
    }

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraVars);
  }

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public ShellProcessHandler.ShellResponse bulkImport(BulkImportParams taskParams) {
    return runCommand(BULK_IMPORT, taskParams);
  }

  public ShellProcessHandler.ShellResponse createBackup(BackupTableParams taskParams) {
    return runCommand(BACKUP, taskParams);
  }
}
