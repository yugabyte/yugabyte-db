// Copyright (c) YugaByte, Inc.

package services;

import org.yb.client.MiniYBCluster;

public interface YBMiniClusterService {
  MiniYBCluster getMiniCluster(int numMasters);
}
