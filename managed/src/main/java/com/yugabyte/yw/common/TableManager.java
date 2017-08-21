// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Singleton
public class TableManager extends DevopsBase {
  private static final int EMR_MULTIPLE = 8;
  private static final String YB_CLOUD_COMMAND_TYPE = "table";
  static final String PY_WRAPPER = "bin/py_wrapper";
  static final String BULK_LOAD_SCRIPT = "bin/yb_bulk_load.py";

  @Inject
  ReleaseManager releaseManager;

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  @Override
  protected String getBaseCommand() {
    return BULK_LOAD_SCRIPT;
  }

  public ShellProcessHandler.ShellResponse tableCommand(BulkImportParams taskParams) {

    Universe universe = Universe.get(taskParams.universeUUID);
    Region region = Region.get(universe.getUniverseDetails().userIntent.regionList.get(0));

    // Grab needed info
    UniverseDefinitionTaskParams.UserIntent userIntent = universe.getUniverseDetails().userIntent;
    String accessKeyCode = userIntent.accessKeyCode;
    AccessKey accessKey = AccessKey.get(region.provider.uuid, accessKeyCode);
    String ybServerPackage = releaseManager.getReleaseByVersion(userIntent.ybSoftwareVersion);
    if (taskParams.instanceCount == 0) {
      taskParams.instanceCount = userIntent.numNodes * EMR_MULTIPLE;
    }

    // Construct bulk import command
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add(PY_WRAPPER);
    commandArgs.add(getBaseCommand());
    commandArgs.add("--key_path");
    commandArgs.add((accessKey == null) ? "yugabyte-default" : accessKey.getKeyInfo().privateKey);
    commandArgs.add("--instance_count");
    commandArgs.add(Integer.toString(taskParams.instanceCount));
    commandArgs.add("--universe");
    commandArgs.add(universe.getUniverseDetails().nodePrefix);
    commandArgs.add("--release");
    commandArgs.add(ybServerPackage);
    commandArgs.add("--masters");
    commandArgs.add(universe.getMasterAddresses());
    commandArgs.add("--table");
    commandArgs.add(taskParams.tableName);
    commandArgs.add("--keyspace");
    commandArgs.add(taskParams.keyspace);
    commandArgs.add("--s3bucket");
    commandArgs.add(taskParams.s3Bucket);

    // Grab necessary environment variables
    Map<String, String> extraVars = region.provider.getConfig();
    extraVars.put("AWS_DEFAULT_REGION", region.code);

    // Execute bulk import command (only valid for AWS right now)
    // TODO: move to opscli

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraVars);
  }
}
