// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.yugabyte.yw.common.TableManager.CommandSubType.BACKUP;
import static com.yugabyte.yw.common.TableManager.CommandSubType.BULK_IMPORT;

@Singleton
public class TableManager extends DevopsBase {
  private static final int EMR_MULTIPLE = 8;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";

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
    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> extraVars = new HashMap<>();

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(subType.getScript());
    commandArgs.add("--masters");
    if (region.provider.code.equals("kubernetes")) {
      commandArgs.add(universe.getKubernetesMasterAddresses());
    } else {
      commandArgs.add(universe.getMasterAddresses());
    }
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
            commandArgs.add("--k8s_namespace");
            commandArgs.add(universe.getUniverseDetails().nodePrefix);
        } else {
          if (accessKey.getKeyInfo().sshUser != null && !accessKey.getKeyInfo().sshUser.isEmpty()) {
            commandArgs.add("--ssh_user");
            commandArgs.add(accessKey.getKeyInfo().sshUser);
          }
          commandArgs.add("--ssh_key_path");
          commandArgs.add(accessKey.getKeyInfo().privateKey);
        }
        commandArgs.add("--s3bucket");
        commandArgs.add(backupTableParams.storageLocation);
        commandArgs.add("--storage_type");
        commandArgs.add(customerConfig.name.toLowerCase());
        if (taskParams.tableUUID != null) {
          commandArgs.add("--table_uuid");
          commandArgs.add(taskParams.tableUUID.toString().replace("-", ""));
        }
        commandArgs.add("--no_auto_name");
        commandArgs.add(backupTableParams.actionType.name().toLowerCase());
        extraVars = customerConfig.dataAsMap();
        if (region.provider.code.equals("kubernetes")) {
          extraVars.putAll(region.provider.getConfig());
        }

        break;
      case BULK_IMPORT:
        BulkImportParams bulkImportParams = (BulkImportParams) taskParams;
        String ybServerPackage = releaseManager.getReleaseByVersion(userIntent.ybSoftwareVersion);
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
