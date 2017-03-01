// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final Logger LOG = LoggerFactory.getLogger(DevopsBase.class);

  @Inject
  ShellProcessHandler shellProcessHandler;

  private List<String> getBaseCommand(UUID providerUUID) {
    Provider cloudProvider = Provider.find.byId(providerUUID);

    List<String> baseCommand = new ArrayList<>();
    baseCommand.add(YBCLOUD_SCRIPT);
    baseCommand.add(cloudProvider.code);

    return baseCommand;
  }

  protected ShellProcessHandler.ShellResponse execCommand(UUID providerUUID,
                                                          List<String> commandArgs) {

    List<String> command = getBaseCommand(providerUUID);
    command.addAll(commandArgs);
    Provider provider = Provider.get(providerUUID);

    LOG.info("Command to run: [" + String.join(" ", command) + "]");
    return shellProcessHandler.run(command, provider.getConfig());
  }
}
