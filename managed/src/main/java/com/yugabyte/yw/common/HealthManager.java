// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import play.libs.Json;

@Singleton
public class HealthManager extends DevopsBase {
  public static final String HEALTH_CHECK_SCRIPT = "bin/cluster_health.py";

  // TODO: we don't need this?
  private static final String YB_CLOUD_COMMAND_TYPE = "health_check";

  public static class ClusterInfo {
    public String identityFile = null;
    public int sshPort;
    public List<String> masterNodes = new ArrayList<>();
    public List<String> tserverNodes = new ArrayList<>(); 
  }

  public ShellProcessHandler.ShellResponse runCommand(
      List<ClusterInfo> clusters, String universeName, String customerTag, String destination,
      boolean shouldSendStatusUpdate) {
    List<String> commandArgs = new ArrayList<>();

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(HEALTH_CHECK_SCRIPT);
    commandArgs.add("--cluster_payload");
    commandArgs.add(Json.stringify(Json.toJson(clusters)));
    commandArgs.add("--universe_name");
    commandArgs.add(universeName);
    commandArgs.add("--customer_tag");
    commandArgs.add(customerTag);
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
