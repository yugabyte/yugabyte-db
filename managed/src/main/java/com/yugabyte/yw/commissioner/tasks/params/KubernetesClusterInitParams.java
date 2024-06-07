// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.Map;

public class KubernetesClusterInitParams extends CloudTaskParams {
  // The config for the target cluster.
  public Map<String, String> config;
}
