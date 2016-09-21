// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import org.yb.client.MiniYBCluster;

public interface YBMiniClusterService {
  MiniYBCluster getMiniCluster(int numMasters);
}
