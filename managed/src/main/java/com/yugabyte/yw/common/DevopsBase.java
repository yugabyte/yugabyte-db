// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final Logger LOG = LoggerFactory.getLogger(DevopsBase.class);

  protected abstract String getCommandType();

  @Inject
  ShellProcessHandler shellProcessHandler;


  private List<String> getBaseCommand(Provider provider, AvailabilityZone az) {
    List<String> baseCommand = new ArrayList<>();
    baseCommand.add(YBCLOUD_SCRIPT);
    baseCommand.add(provider.code);
    baseCommand.add("--zone");
    baseCommand.add(az.code);
    baseCommand.add("--region");
    baseCommand.add(az.region.code);

    return baseCommand;
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID zoneUUID,
                                                          List<String> commandArgs) {
    return execCommand(zoneUUID, commandArgs, Collections.emptyList());
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID zoneUUID,
                                                          List<String> commandArgs,
                                                          List<String> cloudArgs) {
    AvailabilityZone az = AvailabilityZone.get(zoneUUID);
    Provider provider = az.getProvider();
    List<String> command = getBaseCommand(provider, az);
    command.addAll(cloudArgs);
    command.add(getCommandType().toLowerCase());
    command.addAll(commandArgs);

    LOG.info("Command to run: [" + String.join(" ", command) + "]");
    return shellProcessHandler.run(command, provider.getConfig());
  }
}
