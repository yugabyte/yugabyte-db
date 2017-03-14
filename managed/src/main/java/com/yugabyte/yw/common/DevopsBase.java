// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

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

  protected ShellProcessHandler.ShellResponse execCommand(UUID regionUUID,
                                                          List<String> commandArgs) {
    return execCommand(regionUUID, commandArgs, Collections.emptyList());
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID regionUUID,
                                                          List<String> commandArgs,
                                                          List<String> cloudArgs) {
    Region region = Region.get(regionUUID);
    List<String> command = getBaseCommand(region);
    command.addAll(cloudArgs);
    command.add(getCommandType().toLowerCase());
    command.addAll(commandArgs);

    LOG.info("Command to run: [" + String.join(" ", command) + "]");
    return shellProcessHandler.run(command, region.provider.getConfig());
  }
}
