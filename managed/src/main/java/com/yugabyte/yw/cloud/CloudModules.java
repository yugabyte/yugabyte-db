// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import com.google.inject.AbstractModule;
import com.google.inject.multibindings.MapBinder;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.cloud.azu.AZUCloudImpl;
import com.yugabyte.yw.cloud.gcp.GCPCloudImpl;
import com.yugabyte.yw.commissioner.Common.CloudType;

public class CloudModules extends AbstractModule {
  @Override
  protected void configure() {
    MapBinder<String, CloudAPI> mapBinder =
        MapBinder.newMapBinder(binder(), String.class, CloudAPI.class);
    mapBinder.addBinding(CloudType.aws.name()).to(AWSCloudImpl.class);
    mapBinder.addBinding(CloudType.azu.name()).to(AZUCloudImpl.class);
    mapBinder.addBinding(CloudType.gcp.name()).to(GCPCloudImpl.class);
  }
}
