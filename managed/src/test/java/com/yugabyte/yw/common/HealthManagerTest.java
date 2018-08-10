// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

import play.libs.Json;

import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;


@RunWith(MockitoJUnitRunner.class)
public class HealthManagerTest extends FakeDBApplication {
  @Mock
  ShellProcessHandler shellProcessHandler;

  @InjectMocks
  HealthManager healthManager;

  private List<String> healthCheckCommand(
      List<HealthManager.ClusterInfo> clusters, String universeName, String customerTag,
      String destination, boolean shouldSendStatusUpdate) {
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add(DevopsBase.PY_WRAPPER);
    expectedCommand.add(HealthManager.HEALTH_CHECK_SCRIPT);
    expectedCommand.add("--cluster_payload");
    expectedCommand.add(Json.stringify(Json.toJson(clusters)));
    expectedCommand.add("--universe_name");
    expectedCommand.add(universeName);
    expectedCommand.add("--customer_tag");
    expectedCommand.add(customerTag);
    if (destination != null) {
      expectedCommand.add("--destination");
      expectedCommand.add(destination);
    }
    if (shouldSendStatusUpdate) {
      expectedCommand.add("--send_status");
    }
    return expectedCommand;
  }

  @Test
  public void testHealthManager() {
    // Setup the cluster.
    HealthManager.ClusterInfo cluster = new HealthManager.ClusterInfo();
    cluster.sshPort = 22;
    cluster.identityFile = "key.pem";
    cluster.masterNodes = ImmutableList.of("m1", "m2", "m3");
    cluster.tserverNodes = ImmutableList.of("ts1", "ts2", "ts3");
    // Other args
    String universeName = "universe1";
    String customerTag = "customer.env";
    List<String> destinationOptions = new ArrayList<>();
    destinationOptions.add("test@example.com");
    destinationOptions.add(null);
    List<Boolean> statusOptions = ImmutableList.of(true, false);
    for (String d : destinationOptions) {
      for (Boolean sendStatus : statusOptions) {
        List<String> expectedCommand = healthCheckCommand(
            ImmutableList.of(cluster), universeName, customerTag, d, sendStatus);
        healthManager.runCommand(
            ImmutableList.of(cluster), universeName, customerTag, d, sendStatus);
        verify(shellProcessHandler, times(1)).run(expectedCommand, new HashMap<>());
      }
    }
  }
}
