// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

@Singleton
public class NetworkManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(NetworkManager.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "network";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode bootstrap(UUID regionUUID, String hostVpcId, String destVpcId) {
    List<String> commandArgs = new ArrayList();
    if (hostVpcId != null && !hostVpcId.trim().isEmpty()) {
      commandArgs.add("--host_vpc_id");
      commandArgs.add(hostVpcId);
    }
    if (destVpcId != null && !destVpcId.trim().isEmpty()) {
      commandArgs.add("--dest_vpc_id");
      commandArgs.add(destVpcId);
    }

    return execCommand(regionUUID, "bootstrap", commandArgs);
  }

  public JsonNode query(UUID regionUUID) {
    return execCommand(regionUUID, "query", Collections.emptyList());
  }

  public JsonNode cleanup(UUID regionUUID) {
    JsonNode response = execCommand(regionUUID, "cleanup", Collections.emptyList());
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
    return response;
  }
}
