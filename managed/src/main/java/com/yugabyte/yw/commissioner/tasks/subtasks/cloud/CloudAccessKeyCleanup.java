// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

public class CloudAccessKeyCleanup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudAccessKeyCleanup.class);

  public static class Params extends CloudBootstrap.Params {
    public String regionCode;
  }

  @Override
  protected CloudAccessKeyCleanup.Params taskParams() {
    return (CloudAccessKeyCleanup.Params) taskParams;
  }

  @Override
  public void run() {
    String accessKeyCode = String.format("yb-%s-key", getProvider().name).toLowerCase();
    String regionCode = taskParams().regionCode;
    Region region = Region.getByCode(getProvider(), regionCode);
    if (region == null) {
      throw new RuntimeException("Region " +  regionCode + " not setup.");
    }

    AccessManager accessManager = Play.current().injector().instanceOf(AccessManager.class);
    accessManager.deleteKey(region.uuid, accessKeyCode);
  }
}
