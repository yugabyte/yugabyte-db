// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.Provider;

import com.google.inject.Inject;
import com.google.inject.Singleton;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import play.libs.Json;

@Singleton
public class HealthManager extends DevopsBase {
  @Inject
  play.Configuration appConfig;

  public static final String HEALTH_CHECK_SCRIPT = "bin/cluster_health.py";

  // TODO: we don't need this?
  private static final String YB_CLOUD_COMMAND_TYPE = "health_check";

  public static class ClusterInfo {
    public String identityFile = null;
    public int sshPort;
    // TODO: this is to be used by k8s.
    // Note: this is the same across all clusters, so maybe we should pull it out one level above.
    public String nodePrefix = null;
    public List<String> masterNodes = new ArrayList<>();
    public List<String> tserverNodes = new ArrayList<>();
    public String ybSoftwareVersion = null;
    public boolean enableYSQL = false;
    public int ysqlPort = 0;
  }

  public ShellProcessHandler.ShellResponse runCommand(
      Provider provider,
      List<ClusterInfo> clusters,
      String universeName,
      String customerTag,
      String destination,
      Long potentialStartTimeMs,
      Boolean shouldSendStatusUpdate) {
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
    if (potentialStartTimeMs > 0) {
      commandArgs.add("--start_time_ms");
      commandArgs.add(String.valueOf(potentialStartTimeMs));
    }
    if (shouldSendStatusUpdate) {
      commandArgs.add("--send_status");
    }
    // Start with a copy of the cloud config env vars.
    HashMap extraEnvVars = new HashMap<>(provider.getConfig());
    String emailUsername = appConfig.getString("yb.health.ses_email_username");
    if (emailUsername != null) {
      extraEnvVars.put("YB_ALERTS_USERNAME", emailUsername);
    }
    String emailPassword = appConfig.getString("yb.health.ses_email_password");
    if (emailPassword != null) {
      extraEnvVars.put("YB_ALERTS_PASSWORD", emailPassword);
    }

    LOG.info("Command to run: [" + String.join(" ", commandArgs) + "]");
    return shellProcessHandler.run(commandArgs, extraEnvVars);
  }

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }
}
