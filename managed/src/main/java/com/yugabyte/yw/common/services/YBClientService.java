// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import org.yb.client.YBClient;

public interface YBClientService {

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getUniverseClient(Universe universe);

  YBClient getClientWithConfig(YbClientConfig config);

  Optional<String> getServerVersion(YBClient client, String nodeIp, int port);

  Optional<String> getYsqlMajorVersion(YBClient client, String nodeIp, int port);
}
