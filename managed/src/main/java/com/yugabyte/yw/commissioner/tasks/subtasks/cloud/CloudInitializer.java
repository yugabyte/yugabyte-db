// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.yugabyte.yw.cloud.AWSInitializer;
import com.yugabyte.yw.cloud.AbstractInitializer;
import com.yugabyte.yw.cloud.GCPInitializer;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.models.Provider;
import play.api.Play;

public class CloudInitializer extends CloudTaskBase {
  public static class Params extends CloudBootstrap.Params {
    public String regionCode;
  }

  @Override
  protected CloudInitializer.Params taskParams() {
    return (CloudInitializer.Params) taskParams;
  }

  @Override
  public void run() {
    Provider cloudProvider = getProvider();
    AbstractInitializer initializer;
    switch (Common.CloudType.valueOf(cloudProvider.code)) {
      case aws:
        initializer = Play.current().injector().instanceOf(AWSInitializer.class);
        break;
      case gcp:
        initializer = Play.current().injector().instanceOf(GCPInitializer.class);
        break;
      default:
        throw new RuntimeException(cloudProvider.code + " does not have an initializer.");
    }
    initializer.initialize(cloudProvider.customerUUID, cloudProvider.uuid);
  }
}
