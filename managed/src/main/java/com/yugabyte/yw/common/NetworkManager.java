// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
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

  @Inject
  CloudQueryHelper cloudQueryHelper;

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode bootstrap(UUID regionUUID) {
    JsonNode hostInfo = cloudQueryHelper.currentHostInfo(regionUUID, ImmutableList.of("vpc-id"));
    List<String> commandArgs = new ArrayList();
    if (hostInfo.has("vpc-id")) {
      commandArgs.add("--host_vpc_id");
      commandArgs.add(hostInfo.get("vpc-id").asText());
    }
    return execCommand(regionUUID, "bootstrap", commandArgs);
  }

  public JsonNode query(UUID regionUUID) {
    return execCommand(regionUUID, "query", Collections.emptyList());
  }

  public JsonNode cleanup(UUID regionUUID) {
    return execCommand(regionUUID, "cleanup", Collections.emptyList());
  }
}
