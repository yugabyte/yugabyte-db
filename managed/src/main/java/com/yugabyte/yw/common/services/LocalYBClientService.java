// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import java.util.concurrent.CompletableFuture;

import javax.inject.Singleton;

import org.yb.client.YBClient;

import org.yb.util.NetUtil;

import com.google.inject.Inject;

import play.inject.ApplicationLifecycle;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class LocalYBClientService implements YBClientService {
  // For starters hardcode the value, we will be called with required hostports as needed.
  String masterHostPorts = "127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103";
  private YBClient client = null;
  public static final Logger LOG = LoggerFactory.getLogger(LocalYBClientService.class);

  @Inject
  public LocalYBClientService(ApplicationLifecycle lifecycle) {
    client = getNewClient(masterHostPorts);

    lifecycle.addStopHook(() -> {
        client.close();
        return CompletableFuture.completedFuture(null);
    });
  }

  @Override
  public synchronized YBClient getClient(String masterHPs) {
    if (masterHPs != null && !NetUtil.areSameAddresses(masterHostPorts, masterHPs)) {
      LOG.info("Closing client oldMasters={}, newMasters={}.", masterHostPorts, masterHPs);
      try {
        client.close();
      } catch (Exception e) {
        LOG.warn("Closing client with masters={} hit error {}", masterHostPorts, e.getMessage());
      }
      client = getNewClient(masterHPs);
      masterHostPorts = masterHPs;
    }
    return client;
  }

  private YBClient getNewClient(String masterHPs) {
    return new YBClient.YBClientBuilder(masterHPs).build();
  }
}
