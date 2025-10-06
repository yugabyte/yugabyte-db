// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services;

import com.google.inject.Inject;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.VersionInfo;
import org.yb.client.GetStatusResponse;
import org.yb.client.YBClient;

@Slf4j
@Singleton
public class LocalYBClientService implements YBClientService {
  public static final Logger LOG = LoggerFactory.getLogger(LocalYBClientService.class);

  @Inject private YbClientConfigFactory ybClientConfigFactory;

  @Override
  public synchronized YBClient getClient(String masterHostPorts) {
    return getClient(masterHostPorts, null);
  }

  @Override
  public synchronized YBClient getClient(String masterHostPorts, String certFile) {
    if (masterHostPorts != null) {
      return getNewClient(masterHostPorts, certFile);
    }
    return null;
  }

  public synchronized YBClient getUniverseClient(Universe universe) {
    return getClient(universe.getMasterAddresses(), universe.getCertificateNodetoNode());
  }

  private YBClient getNewClient(String masterHPs, String certFile) {
    YbClientConfig config = ybClientConfigFactory.create(masterHPs, certFile);
    return getClientWithConfig(config);
  }

  @Override
  public YBClient getClientWithConfig(YbClientConfig config) {
    if (config == null || StringUtils.isBlank(config.getMasterHostPorts())) {
      return null;
    }
    return new YBClient.YBClientBuilder(config.getMasterHostPorts())
        .sslCertFile(config.getCertFile())
        .defaultAdminOperationTimeoutMs(config.getAdminOperationTimeout().toMillis())
        .defaultOperationTimeoutMs(config.getOperationTimeout().toMillis())
        .defaultSocketReadTimeoutMs(config.getSocketReadTimeout().toMillis())
        .build();
  }

  @Override
  public Optional<String> getServerVersion(YBClient client, String nodeIp, int port) {
    GetStatusResponse response;
    try {
      response = client.getStatus(nodeIp, port);
    } catch (Exception e) {
      log.error("Error fetching version on node " + nodeIp, e);
      return Optional.empty();
    }
    VersionInfo.VersionInfoPB versionInfo = response.getStatus().getVersionInfo();
    return Optional.of(versionInfo.getVersionNumber() + "-b" + versionInfo.getBuildNumber());
  }

  @Override
  public Optional<String> getYsqlMajorVersion(YBClient client, String nodeIp, int port) {
    GetStatusResponse response;
    try {
      response = client.getStatus(nodeIp, port);
    } catch (Exception e) {
      log.error("Error fetching ysql major version on node " + nodeIp, e);
      return Optional.empty();
    }
    VersionInfo.VersionInfoPB versionInfo = response.getStatus().getVersionInfo();
    return versionInfo.hasYsqlMajorVersion()
        ? Optional.of(String.valueOf(versionInfo.getYsqlMajorVersion()))
        : Optional.empty();
  }
}
