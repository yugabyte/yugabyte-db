// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import javax.inject.Singleton;

import org.yb.client.YBClient;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class LocalYBClientService implements YBClientService {
  public static final Logger LOG = LoggerFactory.getLogger(LocalYBClientService.class);

  @Override
  public synchronized YBClient getClient(String masterHostPorts) {
    if (masterHostPorts != null) {
      return getNewClient(masterHostPorts);
    }
    return null;
  }

  @Override
  public synchronized void closeClient(YBClient client, String masterHostPorts) {
    if (client != null) {
      LOG.info("Closing client masters={}.", masterHostPorts);
      try {
        client.close();
      } catch (Exception e) {
        LOG.warn("Closing client with masters={} hit error {}", masterHostPorts, e.getMessage());
      }
    } else {
      LOG.warn("Client for masters {} was null, cannot close", masterHostPorts);
    }
  }

  private YBClient getNewClient(String masterHPs) {
    return new YBClient.YBClientBuilder(masterHPs)
                       .defaultAdminOperationTimeoutMs(120000)
                       .build();
  }
}
