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
import static org.hamcrest.CoreMatchers.*;
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

  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests";
  static String TMP_KEYS_PATH = TMP_STORAGE_PATH + "/keys";

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
    when(appConfig.getString("yb.storage.path")).thenReturn(TMP_STORAGE_PATH);
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
      when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    } else {
      response.code = 0;
      if (commandType.equals("add-key")) {
        String tmpPrivateFile = TMP_KEYS_PATH + "/private.key";
        createTempFile(tmpPrivateFile, "PRIVATE_KEY_FILE");
        String tmpPublicFile = TMP_KEYS_PATH + "/public.key";
        response.message = "{\"public_key\":\""+ tmpPublicFile + "\" ," +
            "\"private_key\": \"" + tmpPrivateFile + "\"}";
        // In case of add-key we make two calls via shellProcessHandler one to add the key,
        // and other call to create a vault file for the keys generated.
        ShellProcessHandler.ShellResponse response2 = new ShellProcessHandler.ShellResponse();
        response2.code = 0;
        response2.message = "{\"vault_file\": \"/path/to/vault_file\"," +
            "\"vault_password\": \"/path/to/vault_password\"}";
        when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response).thenReturn(response2);
      } else {
        if (commandType.equals("create-vault")) {
          response.message = "{\"vault_file\": \"/path/to/vault_file\"," +
              "\"vault_password\": \"/path/to/vault_password\"}";
        } else {
          response.message = "{\"foo\": \"bar\"}";
        }
        when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
      }
    }

    if (commandType.equals("add-key")) {
      return Json.toJson(accessManager.addKey(providerUUID, "foo"));
    } else if (commandType.equals("list-keys")) {
      return accessManager.listKeys(providerUUID);
    } else if (commandType.equals("list-regions")) {
      return accessManager.listRegions(providerUUID);
    } else if (commandType.equals("create-vault")) {
      String tmpPrivateFile = TMP_KEYS_PATH + "/vault-private.key";
      return accessManager.createVault(providerUUID, tmpPrivateFile);
    }
    return null;
  }

  private String getBaseCommand(Provider p, String commandType) {
    return "bin/ybcloud.sh " + p.code + " access " + commandType;
  }

  @Test
  public void testManageAddKeyCommandWithoutProviderConfig() {
    JsonNode json = runCommand(defaultProvider.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(2)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());

    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(getBaseCommand(defaultProvider, "add-key") +
        " --key_pair_name foo --key_file_path " +  TMP_KEYS_PATH + "/" + defaultProvider.uuid);
    expectedCommands.add(getBaseCommand(defaultProvider, "create-vault") +
        " --private_key_file " + TMP_KEYS_PATH + "/private.key");

    List<ArrayList> executedCommands = command.getAllValues();
    for (int idx=0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }
    cloudCredentials.getAllValues().forEach((cloudCredential) -> {
      assertTrue(cloudCredential.isEmpty());
    });
    assertValue(json, "publicKey", TMP_KEYS_PATH + "/public.key");
    assertValue(json, "privateKey", TMP_KEYS_PATH + "/private.key");
    assertValue(json, "vaultFile", "/path/to/vault_file");
    assertValue(json, "vaultPasswordFile", "/path/to/vault_password");
  }

  @Test
  public void testManageAddKeyCommandWithProviderConfig() {
    Map<String, String> config = new HashMap<>();
    config.put("accessKey", "ACCESS-KEY");
    config.put("accessSecret", "ACCESS-SECRET");
    defaultProvider.setConfig(config);
    defaultProvider.save();

    JsonNode json = runCommand(defaultProvider.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(2)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(getBaseCommand(defaultProvider, "add-key") +
        " --key_pair_name foo --key_file_path " + TMP_KEYS_PATH + "/" + defaultProvider.uuid);
    expectedCommands.add(getBaseCommand(defaultProvider, "create-vault") +
        " --private_key_file " + TMP_KEYS_PATH + "/private.key");

    List<ArrayList> executedCommands = command.getAllValues();
    for (int idx=0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }
    cloudCredentials.getAllValues().forEach((cloudCredential) -> {
      assertEquals(config, cloudCredential);
    });
    assertValue(json, "publicKey", TMP_KEYS_PATH + "/public.key");
    assertValue(json, "privateKey", TMP_KEYS_PATH + "/private.key");
    assertValue(json, "vaultFile", "/path/to/vault_file");
    assertValue(json, "vaultPasswordFile", "/path/to/vault_password");
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
    assertValue(result, "privateKey", "test-key.pem");
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
  public void testCreateVaultWithInvalidFile() {
    File file = new File(TMP_KEYS_PATH + "/vault-private.key");
    file.delete();
    try {
      runCommand(defaultProvider.uuid, "create-vault", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("File " + TMP_KEYS_PATH + "/vault-private.key doesn't exists.")));
    }
  }

  @Test
  public void testCreateVaultWithValidFile() {
    createTempFile(TMP_KEYS_PATH + "/vault-private.key", "PRIVATE_KEY_FILE");
    JsonNode result = runCommand(defaultProvider.uuid, "create-vault", false);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(),
        cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultProvider, "create-vault") +
        " --private_key_file " + TMP_KEYS_PATH + "/vault-private.key";
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(cloudCredentials.getValue().isEmpty());
    assertValue(result, "vault_file", "/path/to/vault_file");
    assertValue(result, "vault_password", "/path/to/vault_password");
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

  @Test
  public void testInvalidKeysBasePath() {
    when(appConfig.getString("yb.storage.path")).thenReturn("/foo");
    Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    try {
      runCommand(defaultProvider.uuid, "add-key", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Key path /foo/keys doesn't exists.")));
    }
  }
}
