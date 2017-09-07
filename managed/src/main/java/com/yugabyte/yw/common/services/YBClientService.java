// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import org.yb.client.YBClient;

public interface YBClientService {
  YBClient getClient(String masterHostPorts);
  void closeClient(YBClient client, String masterHostPorts);
}
