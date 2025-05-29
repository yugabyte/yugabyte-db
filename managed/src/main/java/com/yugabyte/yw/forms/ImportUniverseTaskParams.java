// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.validation.constraints.Size;
import play.data.validation.Constraints;

public class ImportUniverseTaskParams extends UniverseTaskParams {
  @Constraints.Required() @JsonProperty public UUID customerUuid;
  @Constraints.Required() @JsonProperty public UUID providerUuid;
  @Constraints.Required() @JsonProperty public String universeName;

  @Constraints.Required()
  @Size(min = 1)
  @JsonProperty
  public Set<String> masterAddrs;

  @JsonProperty public UUID universeUuid;
  @JsonProperty public UUID certUuid; // TODO unused right now.
  @JsonProperty public boolean dedicatedNodes;
  @JsonProperty public boolean userSystemd;
  @JsonProperty public Map<ServerType, ServerConfig> serverConfigs;
  @JsonProperty public Map<String, String> instanceTags;

  public static class ServerConfig {
    @JsonProperty public String serviceName;
    @JsonProperty public Map<String, String> specificGflags;
  }
}
