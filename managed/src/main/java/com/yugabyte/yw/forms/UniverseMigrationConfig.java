// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import lombok.Data;

@Data
public class UniverseMigrationConfig {
  private final Map<ServerType, ServerSpecificConfig> serverConfigs = new ConcurrentHashMap<>();

  public static class ServerSpecificConfig {
    @JsonProperty public boolean userSystemd;
    @JsonProperty public String systemdService;
    @JsonProperty public String confFile;
  }
}
