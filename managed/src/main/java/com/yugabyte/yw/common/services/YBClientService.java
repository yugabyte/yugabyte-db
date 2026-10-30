// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services;

import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import org.yb.client.YBClient;

public interface YBClientService {

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getUniverseClient(Universe universe);

  /** A client for the given masters of universe, presenting its client cert if it requires one. */
  YBClient getUniverseClient(Universe universe, String masterHostPorts);

  YBClient getClientWithConfig(YbClientConfig config);

  Optional<String> getServerVersion(YBClient client, String nodeIp, int port);

  Optional<String> getYsqlMajorVersion(YBClient client, String nodeIp, int port);
}
