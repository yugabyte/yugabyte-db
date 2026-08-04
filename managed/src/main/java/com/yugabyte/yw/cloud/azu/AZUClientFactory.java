// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.azu;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.Provider;

@Singleton
public class AZUClientFactory {
  public AZUResourceGroupApiClient getClient(Provider provider) {
    return new AZUResourceGroupApiClient(provider.getDetails().getCloudInfo().getAzu());
  }
}
