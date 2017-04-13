// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.yugabyte.yw.cloud.AWSInitializer;
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
    AWSInitializer awsInitializer = Play.current().injector().instanceOf(AWSInitializer.class);
    awsInitializer.initialize(cloudProvider.customerUUID, cloudProvider.uuid);
  }
}
