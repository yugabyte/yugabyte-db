// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class CloudQueryHelper extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudQueryHelper.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "query";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode currentHostInfo(UUID regionUUID, List<String> metadataTypes) {
    List<String> commandArgs = new ArrayList<String>();
    commandArgs.add("--metadata_types");
    commandArgs.addAll(metadataTypes);
    return execCommand(regionUUID, "current-host", commandArgs);
  }
}
