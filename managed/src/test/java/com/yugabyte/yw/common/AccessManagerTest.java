// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Provider;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class AccessManagerTest extends FakeDBApplication {

  @InjectMocks
  AccessManager accessManager;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  play.Configuration appConfig;

  private Provider defaultProvider;
  ArgumentCaptor<ArrayList> command;
  ArgumentCaptor<HashMap> cloudCredentials;

  static String TMP_KEYS_PATH = "/tmp/yugaware_tests";

  @BeforeClass
  public static void setUp() {
    new File(TMP_KEYS_PATH).mkdir();
  }

  @AfterClass
  public static void tearDown() {
    File file = new File(TMP_KEYS_PATH);
    file.delete();
  }

  @Before
  public void beforeTest() {
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    when(appConfig.getString("yb.keys.basePath")).thenReturn(TMP_KEYS_PATH);
    command = ArgumentCaptor.forClass(ArrayList.class);
    cloudCredentials = ArgumentCaptor.forClass(HashMap.class);
  }

  private JsonNode uploadKeyCommand(UUID providerUUID, boolean mimicError) {
    if (mimicError) {
      return Json.toJson(accessManager.uploadKeyFile(providerUUID,
          new File("foo"), "test-key", AccessManager.KeyType.PRIVATE));
    } else {
      String tmpFile = createTempFile("SOME DATA");
      return Json.toJson(accessManager.uploadKeyFile(providerUUID,
          new File(tmpFile), "test-key", AccessManager.KeyType.PRIVATE));
    }
  }

  private JsonNode runCommand(UUID providerUUID, String commandType, boolean mimicError) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    if (mimicError) {
      response.message = "{\"error\": \"Unknown Error\"}";
      response.code = 99;
    } else {
      if (commandType.equals("add-key")) {
        response.message = "{\"public_key\": \"/path/to/public.key\"," +
            "\"private_key\": \"/path/to/private.key\"}";
      } else {
        response.message = "{\"foo\": \"bar\"}";
      }
      response.code = 0;
    }
    when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    if (commandType.equals("add-key")) {
      return Json.toJson(accessManager.addKey(providerUUID, "foo"));
    } else if (commandType.equals("list-keys")) {
      return accessManager.listKeys(providerUUID);
    } else if (commandType.equals("list-regions")) {
      return accessManager.listRegions(providerUUID);
    }
    return null;
  }

  private String getBaseCommand(Provider p, String commandType) {
    return "bin/ybcloud.sh " + p.code + " access " + commandType;
  }

  @Test
  public void testManageAddKeyCommandWithoutProviderConfig() {
    JsonNode json = runCommand(defaultProvider.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "add-key") +
        " --key_pair_name foo --key_file_path /tmp/yugaware_tests/" + defaultProvider.uuid;
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(cloudCredentials.getValue().isEmpty());
    assertValue(json, "publicKey", "/path/to/public.key");
    assertValue(json, "privateKey", "/path/to/private.key");
  }

  @Test
  public void testManageAddKeyCommandWithProviderConfig() {
    Map<String, String> config = new HashMap<>();
    config.put("accessKey", "ACCESS-KEY");
    config.put("accessSecret", "ACCESS-SECRET");
    defaultProvider.setConfig(config);
    defaultProvider.save();

    JsonNode json = runCommand(defaultProvider.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "add-key") +
        " --key_pair_name foo --key_file_path /tmp/yugaware_tests/" + defaultProvider.uuid;

    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertEquals(config, cloudCredentials.getValue());
    assertValue(json, "publicKey", "/path/to/public.key");
    assertValue(json, "privateKey", "/path/to/private.key");
  }

  @Test
  public void testManageAddKeyCommandWithErrorResponse() {
    try {
      runCommand(defaultProvider.uuid, "add-key", true);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("AccessManager failed to execute")));
    }
    Mockito.verify(shellProcessHandler, times(1)).run(anyList(), anyMap());
  }

  @Test
  public void testManageListKeysCommand() {
    JsonNode result = runCommand(defaultProvider.uuid, "list-keys", false);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(),
        cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(cloudCredentials.getValue().isEmpty());
    assertValue(result, "foo", "bar");
  }

  @Test
  public void testManageListKeysCommandWithErrorResponse() {
    JsonNode result = runCommand(defaultProvider.uuid, "list-keys", true);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), anyMap());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertValue(result, "error", "AccessManager failed to execute");
  }

  @Test
  public void testManageListRegionsCommand() {
    JsonNode result = runCommand(defaultProvider.uuid, "list-regions", false);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(),
        cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "list-regions");
    ;
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(cloudCredentials.getValue().isEmpty());
    assertValue(result, "foo", "bar");
  }

  @Test
  public void testManageListRegionsCommandWithErrorResponse() {
    JsonNode result = runCommand(defaultProvider.uuid, "list-regions", true);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), anyMap());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "list-regions");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertValue(result, "error", "AccessManager failed to execute");
  }

  @Test
  public void testManageUploadKeyFile() {
    JsonNode result = uploadKeyCommand(defaultProvider.uuid, false);
    System.out.println(result);
  }

  @Test
  public void testManageUploadKeyFileError() {
    try {
      uploadKeyCommand(defaultProvider.uuid, true);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("Key file foo not found.")));
    }
  }


  @Test
  public void testInvalidKeysBasePath() {
    when(appConfig.getString("yb.keys.basePath")).thenReturn(null);
    Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    try {
      runCommand(defaultProvider.uuid, "add-key", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("yb.keys.basePath is null or doesn't exist.")));
    }
  }

  @Test
  public void testKeysBasePathCreateFailure() {
    String tmpFilePath = TMP_KEYS_PATH + "/" + defaultProvider.uuid;
    createTempFile(tmpFilePath, "RANDOM DATA");

    Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    try {
      runCommand(defaultProvider.uuid, "add-key", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("Unable to create key file path " + tmpFilePath)));
    }
  }
}
