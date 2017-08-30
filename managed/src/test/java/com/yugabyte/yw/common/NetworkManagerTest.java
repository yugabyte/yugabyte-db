// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;


import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class NetworkManagerTest extends FakeDBApplication {
  @InjectMocks
  NetworkManager networkManager;

  @Mock
  ShellProcessHandler shellProcessHandler;

  private Provider defaultProvider;
  private Region defaultRegion;
  ArgumentCaptor<ArrayList> command;
  ArgumentCaptor<HashMap> cloudCredentials;

  @Before
  public void beforeTest() {
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    defaultRegion = Region.create(defaultProvider, "us-west-2", "US West 2", "yb-image");
    command = ArgumentCaptor.forClass(ArrayList.class);
    cloudCredentials = ArgumentCaptor.forClass(HashMap.class);
  }

  private JsonNode runBootstrap(UUID regionUUID, String hostVpcId, String destVpcId, boolean mimicError) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    if (mimicError) {
      response.message = "{\"error\": \"Unknown Error\"}";
      response.code = 99;
    } else {
      response.code = 0;
      response.message = "{\"foo\": \"bar\"}";
    }
    when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    return networkManager.bootstrap(regionUUID, hostVpcId, destVpcId);
  }

  private JsonNode runCommand(UUID regionUUID, String commandType, boolean mimicError) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    if (mimicError) {
      response.message = "{\"error\": \"Unknown Error\"}";
      response.code = 99;
    } else {
      response.code = 0;
      response.message = "{\"foo\": \"bar\"}";
    }
    when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);

    if (commandType.equals("query")) {
      return networkManager.query(regionUUID);
    } else if (commandType.equals("cleanup")) {
      return networkManager.cleanup(regionUUID);
    }
    return null;
  }

  @Test
  public void testCommandSuccess() {
    List<String> commandTypes = Arrays.asList("query", "cleanup");
    commandTypes.forEach(commandType -> {
      JsonNode json = runCommand(defaultRegion.uuid, commandType, false);
      assertValue(json, "foo", "bar");
    });
  }

  @Test
  public void testCommandFailure() {
    List<String> commandTypes = Arrays.asList("query", "cleanup");
    commandTypes.forEach(commandType -> {
      try {
        runCommand(defaultRegion.uuid, commandType, true);
      } catch (RuntimeException re) {
        assertEquals(re.getMessage(), "YBCloud command network (" + commandType + ") failed to execute.");
      }
    });
  }

  @Test
  public void testBootstrapCommandWithoutHostVPC() {
    JsonNode json = runBootstrap(defaultRegion.uuid, null, null, false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    assertEquals(String.join(" ", command.getValue()),
        "bin/ybcloud.sh aws --region us-west-2 network bootstrap");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testBootstrapCommandWithHostVPC() {
    JsonNode json = runBootstrap(defaultRegion.uuid, "host-vpc-id", null, false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    assertEquals(String.join(" ", command.getValue()),
        "bin/ybcloud.sh aws --region us-west-2 network bootstrap --host_vpc_id host-vpc-id");
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testBootstrapCommandWithHostVPCAndDestVPC() {
    JsonNode json = runBootstrap(defaultRegion.uuid, "host-vpc-id", "dest-vpc-id", false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    assertEquals(String.join(" ", command.getValue()),
        "bin/ybcloud.sh aws --region us-west-2 network bootstrap --host_vpc_id host-vpc-id " +
        "--dest_vpc_id dest-vpc-id");
    assertValue(json, "foo", "bar");
  }
}
