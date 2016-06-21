// Copyright (c) YugaByte, Inc.

package services;

import org.yb.client.YBClient;

public interface YBClientService {
  YBClient getClient(String masterHostPorts);
}
