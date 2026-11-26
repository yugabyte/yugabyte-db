// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.services;

import com.google.inject.Inject;
import com.yugabyte.yw.common.certmgmt.RpcClientCert;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
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
  private final RpcClientCert rpcClientCert;

  @Inject
  public YbcClientService(RuntimeConfGetter confGetter, RpcClientCert rpcClientCert) {
    this.confGetter = confGetter;
    this.rpcClientCert = rpcClientCert;
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

  private YbcClient getClient(
      String nodeIp, int ybcPort, String certFile, Optional<RpcClientCert.CertAndKey> clientCert) {
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
        clientCert.ifPresent(
            c -> clientBuilder.withClientCertFilepath(c.cert()).withClientKeyFilepath(c.key()));
      }
      return clientBuilder.build();
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Error while creating YbcClient: %s", e.getMessage()));
    }
  }

  /**
   * A client for the YBC on nodeIp of universe. Presents YBA's client cert when the universe
   * requires one, since its YBC then runs with require_client_certificates.
   */
  public YbcClient getNewClient(String nodeIp, int ybcPort, Universe universe) {
    String certFile = universe.getCertificateNodetoNode();
    Optional<RpcClientCert.CertAndKey> clientCert = rpcClientCert.getFiles(universe);
    if (StringUtils.isBlank(certFile)) {
      log.info("Creating ybc client for node: {} on port: {}", nodeIp, ybcPort);
    } else {
      log.info(
          "Creating ybc client for node: {} on port: {} with cert: {}{}",
          nodeIp,
          ybcPort,
          certFile,
          clientCert.map(c -> ", client cert: " + c.cert()).orElse(""));
    }
    return getClient(nodeIp, ybcPort, certFile, clientCert);
  }

  public void closeClient(YbcClient client) {
    if (client != null) {
      client.close();
    }
  }

  public String getYbcServerVersion(String nodeIp, int ybcPort, Universe universe) {
    YbcClient client = null;
    try {
      client = getNewClient(nodeIp, ybcPort, universe);
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
