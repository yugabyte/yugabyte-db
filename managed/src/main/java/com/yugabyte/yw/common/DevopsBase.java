// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final Logger LOG = LoggerFactory.getLogger(DevopsBase.class);

  // Command that we would need to execute eg: instance, network, access.
  protected abstract String getCommandType();

  @Inject
  ShellProcessHandler shellProcessHandler;


  private List<String> getBaseCommand(Region region) {
    List<String> baseCommand = new ArrayList<>();
    baseCommand.add(YBCLOUD_SCRIPT);
    baseCommand.add(region.provider.code);
    baseCommand.add("--region");
    baseCommand.add(region.code);

    return baseCommand;
  }

  protected JsonNode execCommand(UUID regionUUID, String command, List<String> commandArgs) {
    ShellProcessHandler.ShellResponse response = execCommand(regionUUID,
                                                             command,
                                                             commandArgs,
                                                             Collections.emptyList());
    if (response.code == 0) {
      return Json.parse(response.message);
    } else {
      LOG.error(response.message);
      return ApiResponse.errorJSON("YBCloud command " + getCommandType() + " (" + command + ") failed to execute.");
    }
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID regionUUID,
                                                          String command,
                                                          List<String> commandArgs,
                                                          List<String> cloudArgs) {
    Region region = Region.get(regionUUID);
    List<String> commandList = getBaseCommand(region);
    commandList.addAll(cloudArgs);
    commandList.add(getCommandType().toLowerCase());
    commandList.add(command);
    commandList.addAll(commandArgs);

    LOG.info("Command to run: [" + String.join(" ", commandList) + "]");
    return shellProcessHandler.run(commandList, region.provider.getConfig());
  }
}
