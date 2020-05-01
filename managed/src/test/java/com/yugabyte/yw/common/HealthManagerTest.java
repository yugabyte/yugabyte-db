// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Provider;

import com.google.common.collect.ImmutableList;
import org.junit.Before;
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
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class HealthManagerTest extends FakeDBApplication {
  @Mock
  ShellProcessHandler shellProcessHandler;

  @InjectMocks
  HealthManager healthManager;

  @Mock
  play.Configuration appConfig;

  private List<String> healthCheckCommand(
      Provider provider, List<HealthManager.ClusterInfo> clusters, String universeName,
      String customerTag, String destination, long startTimeMs,
      boolean shouldSendStatusUpdate, boolean reportOnlyErrors) {
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
    if (startTimeMs > 0) {
      expectedCommand.add("--start_time_ms");
      expectedCommand.add(String.valueOf(startTimeMs));
    }
    if (shouldSendStatusUpdate) {
      expectedCommand.add("--send_status");
    }
    if (reportOnlyErrors) {
      expectedCommand.add("--report_only_errors");
    }
    return expectedCommand;
  }

  @Test
  public void testHealthManager() {
    HashMap<String, String> baseConfig = new HashMap<>();
    baseConfig.put("testKey", "testVal");
    Provider provider = ModelFactory.newProvider(
        ModelFactory.testCustomer(), Common.CloudType.aws, baseConfig);
    // Setup the cluster.
    HealthManager.ClusterInfo cluster = new HealthManager.ClusterInfo();
    cluster.sshPort = 22;
    cluster.identityFile = "key.pem";
    cluster.masterNodes = new HashMap<>();
    cluster.masterNodes.put("m1", "m1-name");
    cluster.masterNodes.put("m2", "m2-name");
    cluster.masterNodes.put("m3", "m3-name");
    cluster.tserverNodes = new HashMap<>();
    cluster.tserverNodes.put("ts1", "ts1-name");
    cluster.tserverNodes.put("ts2", "ts2-name");
    cluster.tserverNodes.put("ts3", "ts3-name");
    // Other args
    String universeName = "universe1";
    String customerTag = "customer.env";
    // Destination options.
    List<String> destinationOptions = new ArrayList<>();
    destinationOptions.add("test@example.com");
    destinationOptions.add(null);
    // --send_status options.
    List<Boolean> statusOptions = ImmutableList.of(true, false);
    // --start_time_ms options.
    List<Long> startTimeOptions = ImmutableList.of(0L, 1000L);
    List<String> envVarOptions = new ArrayList<>();
    envVarOptions.add("testing");
    envVarOptions.add(null);
    List<Boolean> reportOnlyErrorOptions = ImmutableList.of(true, false);
    for (String d : destinationOptions) {
      for (Boolean sendStatus : statusOptions) {
        for (Long startTime : startTimeOptions) {
          for (String envVal : envVarOptions) {
            for (Boolean reportOnlyErrors : reportOnlyErrorOptions) {
              when(appConfig.getString("yb.health.ses_email_username")).thenReturn(envVal);
              when(appConfig.getString("yb.health.ses_email_password")).thenReturn(envVal);
              when(appConfig.getString("yb.health.default_email")).thenReturn(envVal);
              List<String> expectedCommand = healthCheckCommand(
                  provider, ImmutableList.of(cluster), universeName, customerTag, d, startTime,
                  sendStatus, reportOnlyErrors);
            System.out.println("running, reportOnlyErrors = " + reportOnlyErrors.toString());
             healthManager.runCommand(
                  provider, ImmutableList.of(cluster), universeName, customerTag, d, startTime,
                  sendStatus, reportOnlyErrors, null);
              HashMap extraEnvVars = new HashMap<>(provider.getConfig());
              if (envVal != null) {
                extraEnvVars.put("YB_ALERTS_USERNAME", envVal);
                extraEnvVars.put("YB_ALERTS_PASSWORD", envVal);
                extraEnvVars.put("YB_ALERTS_EMAIL", envVal);
              }
              System.out.println("verifying");
              verify(shellProcessHandler, times(1)).run(expectedCommand, extraEnvVars, false);
            }
          }
        }
      }
    }
  }
}
