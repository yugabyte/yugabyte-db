// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static org.mockito.Matchers.anyString;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Provider;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class HealthManagerTest extends FakeDBApplication {
  @Mock private ShellProcessHandler shellProcessHandler;

  @InjectMocks private HealthManager healthManager;

  @Mock private play.Configuration appConfig;

  private static final String[] providers = {"aws", "gcp", "onprem", "kubernetes"};

  private List<String> healthCheckCommand(
      Provider provider,
      List<HealthManager.ClusterInfo> clusters,
      String customerTag,
      String destination,
      long startTimeMs,
      boolean shouldSendStatusUpdate,
      boolean reportOnlyErrors) {
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add(DevopsBase.PY_WRAPPER);
    expectedCommand.add(HealthManager.HEALTH_CHECK_SCRIPT);

    expectedCommand.add("--cluster_payload");
    expectedCommand.add(Json.stringify(Json.toJson(clusters)));
    if (startTimeMs > 0) {
      expectedCommand.add("--start_time_ms");
      expectedCommand.add(String.valueOf(startTimeMs));
    }
    if (!provider.code.equals("onprem") && !provider.code.equals("kubernetes")) {
      expectedCommand.add("--check_clock");
    }
    return expectedCommand;
  }

  @Test
  public void testHealthManager() {
    HashMap<String, String> baseConfig = new HashMap<>();
    baseConfig.put("testKey", "testVal");
    Provider provider =
        ModelFactory.newProvider(ModelFactory.testCustomer(), Common.CloudType.aws, baseConfig);
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
    String customerTag = "customer.env";
    // Destination options.
    List<String> destinationOptions = new ArrayList<>();
    destinationOptions.add("test@example.com");
    destinationOptions.add(null);
    // --send_status options.
    List<Boolean> statusOptions = ImmutableList.of(true, false);
    // --start_time_ms options.
    List<Long> startTimeOptions = ImmutableList.of(0L, 1000L);
    // --check_clock options.
    List<String> envVarOptions = new ArrayList<>();
    envVarOptions.add("testing");
    envVarOptions.add(null);
    List<Boolean> reportOnlyErrorOptions = ImmutableList.of(true, false);
    for (String d : destinationOptions) {
      for (Boolean sendStatus : statusOptions) {
        for (Long startTime : startTimeOptions) {
          for (String envVal : envVarOptions) {
            for (Boolean reportOnlyErrors : reportOnlyErrorOptions) {
              for (String providerCode : providers) {
                provider.code = providerCode;
                when(appConfig.getString("yb.health.ses_email_username")).thenReturn(envVal);
                when(appConfig.getString("yb.health.ses_email_password")).thenReturn(envVal);
                when(appConfig.getString("yb.health.default_email")).thenReturn(envVal);
                List<String> expectedCommand =
                    healthCheckCommand(
                        provider,
                        ImmutableList.of(cluster),
                        customerTag,
                        d,
                        startTime,
                        sendStatus,
                        reportOnlyErrors);
                healthManager.runCommand(provider, ImmutableList.of(cluster), startTime, false);
                HashMap extraEnvVars = new HashMap<>(provider.getUnmaskedConfig());
                verify(shellProcessHandler, times(1))
                    .run(eq(expectedCommand), eq(extraEnvVars), eq(false), anyString());

                reset(shellProcessHandler);
              }
            }
          }
        }
      }
    }
  }
}
