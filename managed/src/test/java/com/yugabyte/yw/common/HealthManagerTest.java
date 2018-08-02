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

import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;


@RunWith(MockitoJUnitRunner.class)
public class HealthManagerTest extends FakeDBApplication {
  @Mock
  ShellProcessHandler shellProcessHandler;

  @InjectMocks
  HealthManager healthManager;

  private List<String> healthCheckCommand(
      String mastersCsv, String tserversCsv, String sshPort, String universeName, String privateKey,
      String customerTag, String destination, boolean shouldSendStatusUpdate) {
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add(DevopsBase.PY_WRAPPER);
    expectedCommand.add(HealthManager.HEALTH_CHECK_SCRIPT);
    expectedCommand.add("--master_nodes");
    expectedCommand.add(mastersCsv);
    expectedCommand.add("--tserver_nodes");
    expectedCommand.add(tserversCsv);
    expectedCommand.add("--ssh_port");
    expectedCommand.add(sshPort);
    expectedCommand.add("--universe_name");
    expectedCommand.add(universeName);
    expectedCommand.add("--identity_file");
    expectedCommand.add(privateKey);
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
    String mastersCsv = "m1,m2,m3";
    String tserversCsv = "ts1,ts2,ts3";
    String sshPort = "22";
    String universeName = "universe1";
    String privateKey = "key.pem";
    String customerTag = "customer.env";
    List<String> destinationOptions = new ArrayList<>();
    destinationOptions.add("test@example.com");
    destinationOptions.add(null);
    List<Boolean> statusOptions = ImmutableList.of(true, false);
    for (String d : destinationOptions) {
      for (Boolean sendStatus : statusOptions) {
        List<String> expectedCommand = healthCheckCommand(
            mastersCsv, tserversCsv, sshPort, universeName, privateKey, customerTag, d, sendStatus);
        healthManager.runCommand(
            mastersCsv, tserversCsv, sshPort, universeName, privateKey, customerTag, d, sendStatus);
        verify(shellProcessHandler, times(1)).run(expectedCommand, new HashMap<>());
      }
    }
  }
}
