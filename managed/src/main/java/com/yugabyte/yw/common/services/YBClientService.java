// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services;

import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import org.yb.client.YBClientApi;

public interface YBClientService {

  YBClientApi getClient(String masterHostPorts);

  YBClientApi getClient(String masterHostPorts, String certFile);

  YBClientApi getUniverseClient(Universe universe);

  /** A client for the given masters of universe, presenting its client cert if it requires one. */
  YBClientApi getUniverseClient(Universe universe, String masterHostPorts);

  YBClientApi getClientWithConfig(YbClientConfig config);

  Optional<String> getServerVersion(YBClientApi client, String nodeIp, int port);

  Optional<String> getYsqlMajorVersion(YBClientApi client, String nodeIp, int port);
}
