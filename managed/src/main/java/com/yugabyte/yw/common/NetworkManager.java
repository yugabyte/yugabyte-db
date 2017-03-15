// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.Collections;
import java.util.UUID;

@Singleton
public class NetworkManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(NetworkManager.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "network";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode bootstrap(UUID regionUUID) {
    return execCommand(regionUUID, "bootstrap", Collections.emptyList());
  }

  public JsonNode query(UUID regionUUID) {
    return execCommand(regionUUID, "query", Collections.emptyList());
  }

  public JsonNode cleanup(UUID regionUUID) {
    return execCommand(regionUUID, "cleanup", Collections.emptyList());
  }
}
