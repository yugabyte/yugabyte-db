// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import org.yb.client.YBClient;

public interface YBClientService {
  YBClient getClient(String masterHostPorts);
  YBClient getClient(String masterHostPorts, String rootCertFile);
  YBClient getClient(String masterHostPorts, String rootCertFile, String[] rpcClientCertFiles);
  void closeClient(YBClient client, String masterHostPorts);
}
