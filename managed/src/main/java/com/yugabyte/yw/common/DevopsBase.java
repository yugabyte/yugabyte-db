// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final String PY_WRAPPER = "bin/py_wrapper";
  public static final Logger LOG = LoggerFactory.getLogger(DevopsBase.class);

  // Command that we would need to execute eg: instance, network, access.
  protected abstract String getCommandType();

  protected String getBaseCommand() {
    return YBCLOUD_SCRIPT;
  }

  @Inject
  ShellProcessHandler shellProcessHandler;

  protected JsonNode parseShellResponse(ShellProcessHandler.ShellResponse response, String command) {
    if (response.code == 0) {
      return Json.parse(response.message);
    } else {
      LOG.error(response.message);
      return ApiResponse.errorJSON("YBCloud command " + getCommandType() + " (" + command + ") failed to execute.");
    }
  }

  protected JsonNode execAndParseCommand(Common.CloudType cloudType, String command, List<String> commandArgs) {
    ShellProcessHandler.ShellResponse response = execCommand(new UUID(0L, 0L), command,
        commandArgs, cloudType, Collections.emptyList());
    // WARNING: Does not pass environment variables from config. 
    return parseShellResponse(response, command);
  }

  protected JsonNode execAndParseCommand(UUID regionUUID, String command, List<String> commandArgs) {
    ShellProcessHandler.ShellResponse response = execCommand(regionUUID, command, commandArgs,
        null, Collections.emptyList());
    return parseShellResponse(response, command);
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID regionUUID,
                                                          String command,
                                                          List<String> commandArgs,
                                                          List<String> cloudArgs) {
    return execCommand(regionUUID, command, commandArgs, null, cloudArgs);
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID regionUUID,
                                                          String command,
                                                          List<String> commandArgs,
                                                          Common.CloudType cloudType,
                                                          List<String> cloudArgs) {
    List<String> commandList = new ArrayList<>();
    commandList.add(YBCLOUD_SCRIPT);
    Map<String, String> extraVars = new HashMap<>();
    Region region = Region.get(regionUUID);
    if (region != null) {
      commandList.add(region.provider.code);
      commandList.add("--region");
      commandList.add(region.code);
      extraVars = region.provider.getConfig();
    } else if (cloudType != null) {
      commandList.add(cloudType.toString());
    } else {
      throw new RuntimeException("Invalid args provided for execCommand: RegionUUID or " +
          "CloudType required.");
    }

    commandList.addAll(cloudArgs);
    commandList.add(getCommandType().toLowerCase());
    commandList.add(command);
    commandList.addAll(commandArgs);

    LOG.info("Command to run: [" + String.join(" ", commandList) + "]");
    return shellProcessHandler.run(commandList, extraVars);
  }
}
