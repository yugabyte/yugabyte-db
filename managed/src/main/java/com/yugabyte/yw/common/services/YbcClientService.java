// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.VersionRequest;
import org.yb.ybc.VersionResponse;

@Singleton
@Slf4j
public class YbcClientService {

  private YbcClient getClient(String nodeIp, int ybcPort) {
    try {
      log.info("Creating ybc client for node: {} on port: {}", nodeIp, ybcPort);
      return new YbcClient(nodeIp, ybcPort);
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Error while creating YbcClient: %s", e.getMessage()));
    }
  }

  private YbcClient getClient(String nodeIp, int ybcPort, String certFile) {
    try {
      log.info(
          "Creating ybc client for node: {} on port: {} with cert: {}", nodeIp, ybcPort, certFile);
      return new YbcClient(nodeIp, ybcPort, certFile);
    } catch (Exception e) {
      throw new RuntimeException(
          String.format("Error while creating YbcClient: %s", e.getMessage()));
    }
  }

  public YbcClient getNewClient(String nodeIp, int ybcPort, String certFile) {
    if (certFile == null) {
      return getClient(nodeIp, ybcPort);
    }
    return getClient(nodeIp, ybcPort, certFile);
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
