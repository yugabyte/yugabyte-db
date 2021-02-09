/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;

@Singleton
public class PlatformInstanceClientFactory {
  @Inject
  ApiHelper apiHelper;

  public PlatformInstanceClient getClient(String clusterKey, String remoteAddress) {
    return new PlatformInstanceClient(this.apiHelper, clusterKey, remoteAddress);
  }
}
