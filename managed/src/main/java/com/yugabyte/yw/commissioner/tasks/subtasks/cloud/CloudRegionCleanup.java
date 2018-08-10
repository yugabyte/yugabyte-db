// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

public class CloudRegionCleanup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudRegionCleanup.class);

  public static class Params extends CloudTaskParams {
    public String regionCode;
  }

  @Override
  protected CloudRegionCleanup.Params taskParams() {
    return (CloudRegionCleanup.Params)taskParams;
  }

  @Override
  public void run() {
    String regionCode = taskParams().regionCode;
    Region region = Region.getByCode(getProvider(), regionCode);
    if (region == null) {
      throw new RuntimeException("Region " +  regionCode + " doesn't exists.");
    }
    NetworkManager networkManager = Play.current().injector().instanceOf(NetworkManager.class);

    JsonNode vpcInfo = networkManager.cleanup(region.uuid);
    if (vpcInfo.has("error") || !vpcInfo.has(regionCode)) {
      throw new RuntimeException("Region cleanup failed for: " + regionCode);
    }
    region.delete();
  }
}
