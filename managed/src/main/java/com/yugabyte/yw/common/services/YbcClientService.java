// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.YbcClient;
import org.yb.client.YbcClient.YbcClientBuilder;
import org.yb.ybc.VersionRequest;
import org.yb.ybc.VersionResponse;

@Singleton
@Slf4j
public class YbcClientService {

  private final RuntimeConfGetter confGetter;

  @Inject
  public YbcClientService(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  private Integer getMaxUnavailableRetries() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientMaxUnavailableRetries);
  }

  private Integer getWaitEachUnavailableRetryMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientWaitEachUnavailableRetryMs);
  }

  private Integer getMaxInboundMsgSize() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientMaxInboundMsgSize);
  }

  private Integer getDeadlineMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientDeadlineMs);
  }

  private Integer getMaxKeepAlivePingsMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientKeepAlivePingsMs);
  }

  private Integer getMaxKeepAlivePingsTimeoutMs() {
    return confGetter.getGlobalConf(GlobalConfKeys.ybcClientKeepAlivePingsTimeoutMs);
  }

  private YbcClient getClient(String nodeIp, int ybcPort, String certFile) {
    try {
      YbcClientBuilder clientBuilder = YbcClient.builder();
      clientBuilder
          .withYbcIp(nodeIp)
          .withYbcPort(ybcPort)
          .withNumRetries(getMaxUnavailableRetries())
          .withWaitEachRetryMs(getWaitEachUnavailableRetryMs())
          .withMaxInboundSize(getMaxInboundMsgSize())
          .withDeadlineMs(getDeadlineMs())
          .withKeepAliveMs(getMaxKeepAlivePingsMs())
          .withKeepAliveTimeoutMs(getMaxKeepAlivePingsTimeoutMs());
      if (StringUtils.isNotBlank(certFile)) {
        clientBuilder.withCertsFilepath(certFile);
      }
      return clientBuilder.build();
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Error while creating YbcClient: %s", e.getMessage()));
    }
  }

  public YbcClient getNewClient(String nodeIp, int ybcPort, String certFile) {
    if (StringUtils.isBlank(certFile)) {
      log.info("Creating ybc client for node: {} on port: {}", nodeIp, ybcPort);
      return getClient(nodeIp, ybcPort, null);
    } else {
      log.info(
          "Creating ybc client for node: {} on port: {} with cert: {}", nodeIp, ybcPort, certFile);
      return getClient(nodeIp, ybcPort, certFile);
    }
  }

  public void closeClient(YbcClient client) {
    if (client != null) {
      client.close();
    }
  }

  public String getYbcServerVersion(String nodeIp, int ybcPort, String certFile) {
    YbcClient client = null;
    try {
      client = getNewClient(nodeIp, ybcPort, certFile);
      VersionRequest req = VersionRequest.newBuilder().build();
      VersionResponse resp = client.version(req);
      String version = resp.getServerVersion();
      return version;
    } catch (Exception e) {
      throw e;
    } finally {
      closeClient(client);
    }
  }
}
