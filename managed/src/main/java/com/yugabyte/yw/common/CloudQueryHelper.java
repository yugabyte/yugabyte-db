// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;

@Singleton
public class CloudQueryHelper extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudQueryHelper.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "query";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode currentHostInfo(Common.CloudType cloudType, List<String> metadataTypes) {
    List<String> commandArgs = new ArrayList<String>();
    commandArgs.add("--metadata_types");
    commandArgs.addAll(metadataTypes);
    return execCommand(cloudType, "current-host", commandArgs);
  }

  public JsonNode getZones(Region region) {
  	List<String> commandArgs = new ArrayList<String>();
  	commandArgs.add("--regions");
  	commandArgs.add(region.code);
    return execCommand(region.uuid, "zones", commandArgs);
  }

  /*
   * Example return from GCP:
   * {
   *   "n1-standard-32": {
   *     "prices": {
   *         "us-west1": [
   *         {
   *           "price": 1.52,
   *           "os": "Linux"
   *         }
   *       ],
   *       "us-east1": [
   *         {
   *           "price": 1.52,
   *           "os": "Linux"
   *         }
   *       ]
   *     },
   *     "numCores": 32,
   *     "description": "32 vCPUs, 120 GB RAM",
   *     "memSizeGb": 120,
   *     "isShared": false
   *   },
   *   ...
   * }
   */
  public JsonNode getInstanceTypes(Common.CloudType cloudType, List<Region> regionList) {
  	List<String> commandArgs = new ArrayList<String>();
  	commandArgs.add("--regions");
  	regionList.forEach(region -> commandArgs.add(region.code));
  	return execCommand(regionList.get(0).uuid, "instance_types", commandArgs);
  }
}
