// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NetworkManagerTest extends FakeDBApplication {
  @InjectMocks NetworkManager networkManager;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock RuntimeConfGetter mockConfGetter;

  private Provider defaultProvider;
  private Region defaultRegion;
  ArgumentCaptor<List<String>> command;

  @Before
  public void beforeTest() {
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    defaultRegion = Region.create(defaultProvider, "us-west-2", "US West 2", "yb-image");
    command = ArgumentCaptor.forClass(List.class);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
  }

  private JsonNode runBootstrap(
      UUID regionUUID, UUID providerUUID, String customPayload, boolean mimicError) {
    ShellResponse response = new ShellResponse();
    if (mimicError) {
      response.message = "Unknown error occurred";
      response.code = 99;
    } else {
      response.code = 0;
      response.message = "{\"foo\": \"bar\"}";
    }
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    return networkManager.bootstrap(regionUUID, providerUUID, customPayload);
  }

  private JsonNode runCommand(UUID regionUUID, String commandType, boolean mimicError) {
    ShellResponse response = new ShellResponse();
    if (mimicError) {
      response.message = "Unknown error occurred";
      response.code = 99;
    } else {
      response.code = 0;
      response.message = "{\"foo\": \"bar\"}";
    }
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);

    if (commandType.equals("query")) {
      return networkManager.query(regionUUID, "");
    } else if (commandType.equals("cleanup")) {
      return networkManager.cleanupOrFail(regionUUID);
    }
    return null;
  }

  @Test
  public void testCommandSuccess() {
    List<String> commandTypes = Arrays.asList("query", "cleanup");
    commandTypes.forEach(
        commandType -> {
          JsonNode json = runCommand(defaultRegion.getUuid(), commandType, false);
          assertValue(json, "foo", "bar");
        });
  }

  @Test
  public void testCommandFailure() {
    List<String> commandTypes = Arrays.asList("query", "cleanup");
    commandTypes.forEach(
        commandType -> {
          try {
            runCommand(defaultRegion.getUuid(), commandType, true);
          } catch (RuntimeException re) {
            assertEquals(
                re.getMessage(),
                "YBCloud command network ("
                    + commandType
                    + ") failed to execute. Unknown error occurred");
          }
        });
  }

  @Test
  public void testBootstrapCommandWithProvider() {
    JsonNode json = runBootstrap(null, defaultRegion.getProvider().getUuid(), "{}", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));
    assertEquals(String.join(" ", command.getValue()), "bin/ybcloud.sh aws network bootstrap");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testGcpBootstrapCommandWithProvider() {
    Provider gcpProvider = ModelFactory.gcpProvider(ModelFactory.testCustomer());
    Region gcpRegion = Region.create(gcpProvider, "us-west1", "US West1", "yb-image");
    JsonNode json = runBootstrap(null, gcpRegion.getProvider().getUuid(), "{}", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));
    assertEquals(String.join(" ", command.getValue()), "bin/ybcloud.sh gcp network bootstrap");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testGcpBootstrapCommandWithPayload() {
    Provider gcpProvider = ModelFactory.gcpProvider(ModelFactory.testCustomer());
    Region gcpRegion = Region.create(gcpProvider, "us-west1", "US West1", "yb-image");
    String payload = "{\"region\": \"gcptest\"}";
    JsonNode json = runBootstrap(null, gcpRegion.getProvider().getUuid(), payload, false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));
    assertEquals(String.join(" ", command.getValue()), "bin/ybcloud.sh gcp network bootstrap");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testBootstrapCommandWithRegion() {
    JsonNode json = runBootstrap(defaultRegion.getUuid(), null, "{}", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));
    assertEquals(
        String.join(" ", command.getValue()),
        "bin/ybcloud.sh aws --region us-west-2 network bootstrap");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testBootstrapCommandWithRegionAndProvider() {
    // If both are provided, we first check for region and use --region if available.
    JsonNode json =
        runBootstrap(defaultRegion.getUuid(), defaultRegion.getProvider().getUuid(), "{}", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));
    assertEquals(
        String.join(" ", command.getValue()),
        "bin/ybcloud.sh aws --region us-west-2 network bootstrap");
    assertValue(json, "foo", "bar");
  }
}
