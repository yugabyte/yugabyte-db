// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

@Singleton
public class HealthManager extends DevopsBase {
  public static final String HEALTH_CHECK_SCRIPT = "bin/cluster_health.py";

  // TODO: we don't need this?
  private static final String YB_CLOUD_COMMAND_TYPE = "health_check";

  public ShellProcessHandler.ShellResponse runCommand(
      String mastersCsv, String tserversCsv, String sshPort, String universeName,
      String privateKey, String destination, boolean shouldSendStatusUpdate) {
    List<String> commandArgs = new ArrayList<>();

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(HEALTH_CHECK_SCRIPT);
    commandArgs.add("--master_nodes");
    commandArgs.add(mastersCsv);
    commandArgs.add("--tserver_nodes");
    commandArgs.add(tserversCsv);
    commandArgs.add("--ssh_port");
    commandArgs.add(sshPort);
    commandArgs.add("--universe_name");
    commandArgs.add(universeName);
    commandArgs.add("--identity_file");
    commandArgs.add(privateKey);
    if (destination != null) {
      commandArgs.add("--destination");
      commandArgs.add(destination);
    }
    if (shouldSendStatusUpdate) {
      commandArgs.add("--send_status");
    }

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, new HashMap<>());
  }

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }
}
