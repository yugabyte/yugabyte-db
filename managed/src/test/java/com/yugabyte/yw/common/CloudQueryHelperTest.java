// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class CloudQueryHelperTest extends FakeDBApplication {

  @InjectMocks
  CloudQueryHelper cloudQueryHelper;

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

  private JsonNode runCommand(UUID regionUUID, boolean mimicError) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    if (mimicError) {
      response.message = "{\"error\": \"Unknown Error\"}";
      response.code = 99;
    } else {
      response.code = 0;
      response.message = "{\"foo\": \"bar\"}";
    }
    when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    return cloudQueryHelper.currentHostInfo(Common.CloudType.aws, ImmutableList.of("vpc-id"));
  }

  @Test
  public void testGetHostInfoSuccess() {
    JsonNode json = runCommand(defaultRegion.uuid, false);
    assertValue(json, "foo", "bar");
  }

  @Test
  public void testGetHostInfoFailure() {
    JsonNode json = runCommand(defaultRegion.uuid, true);
    assertErrorNodeValue(json, "YBCloud command query (current-host) failed to execute.");
  }
}
