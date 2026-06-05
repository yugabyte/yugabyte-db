// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.gcp;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;

@Singleton
public class GCPProjectApiClientFactory {

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public GCPProjectApiClientFactory(RuntimeConfGetter runtimeConfGetter) {
    this.runtimeConfGetter = runtimeConfGetter;
  }

  public GCPProjectApiClient getClient(Provider provider) {
    return new GCPProjectApiClient(runtimeConfGetter, provider);
  }
}
