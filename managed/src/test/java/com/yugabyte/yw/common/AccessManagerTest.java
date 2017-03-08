// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.junit.Before;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
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
  private Region defaultRegion;
  ArgumentCaptor<ArrayList> command;
  ArgumentCaptor<HashMap> cloudCredentials;

  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests";
  static String TMP_KEYS_PATH = TMP_STORAGE_PATH + "/keys";

  @Before
  public void beforeTest() {
    new TemporaryFolder(new File(TMP_KEYS_PATH));
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    defaultRegion = Region.create(defaultProvider, "us-west-2", "US West 2", "yb-image");
    AvailabilityZone.create(defaultRegion, "az-1", "az", "subnet-1");
    when(appConfig.getString("yb.storage.path")).thenReturn(TMP_STORAGE_PATH);
    command = ArgumentCaptor.forClass(ArrayList.class);
    cloudCredentials = ArgumentCaptor.forClass(HashMap.class);
  }

  private JsonNode uploadKeyCommand(UUID regionUUID, boolean mimicError) {
    if (mimicError) {
      return Json.toJson(accessManager.uploadKeyFile(regionUUID,
          new File("foo"), "test-key", AccessManager.KeyType.PRIVATE));
    } else {
      String tmpFile = createTempFile("SOME DATA");
      return Json.toJson(accessManager.uploadKeyFile(regionUUID,
          new File(tmpFile), "test-key", AccessManager.KeyType.PRIVATE));
    }
  }

  private JsonNode runCommand(UUID regionUUID, String commandType, boolean mimicError) {
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    if (mimicError) {
      response.message = "{\"error\": \"Unknown Error\"}";
      response.code = 99;
      when(shellProcessHandler.run(anyList(), anyMap())).thenReturn(response);
    } else {
      response.code = 0;
      if (commandType.equals("add-key")) {
        String tmpPrivateFile = TMP_KEYS_PATH + "/private.key";
        createTempFile("keys/private.key", "PRIVATE_KEY_FILE");
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
      return Json.toJson(accessManager.addKey(regionUUID, "foo"));
    } else if (commandType.equals("list-keys")) {
      return accessManager.listKeys(regionUUID);
    } else if (commandType.equals("create-vault")) {
      String tmpPrivateFile = TMP_KEYS_PATH + "/vault-private.key";
      return accessManager.createVault(regionUUID, tmpPrivateFile);
    }
    return null;
  }

  private String getBaseCommand(Region region, String commandType) {
    AvailabilityZone az = AvailabilityZone.getAZsForRegion(region.uuid).get(0);
    return "bin/ybcloud.sh " + region.provider.code +
        " --zone " + az.code + " --region " + region.code +
        " access " + commandType;
  }

  @Test
  public void testManageAddKeyCommandWithoutProviderConfig() {
    JsonNode json = runCommand(defaultRegion.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(2)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());

    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(getBaseCommand(defaultRegion, "add-key") +
        " --key_pair_name foo --key_file_path " +  TMP_KEYS_PATH + "/" + defaultProvider.uuid);
    expectedCommands.add(getBaseCommand(defaultRegion, "create-vault") +
        " --private_key_file " + TMP_KEYS_PATH + "/private.key");

    List<ArrayList> executedCommands = command.getAllValues();
    for (int idx=0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }
    cloudCredentials.getAllValues().forEach((cloudCredential) -> {
      assertTrue(cloudCredential.isEmpty());
    });
    assertValidAccessKey(json);
  }

  @Test
  public void testManageAddKeyCommandWithProviderConfig() {
    Map<String, String> config = new HashMap<>();
    config.put("accessKey", "ACCESS-KEY");
    config.put("accessSecret", "ACCESS-SECRET");
    defaultProvider.setConfig(config);
    defaultProvider.save();

    JsonNode json = runCommand(defaultRegion.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(2)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(getBaseCommand(defaultRegion, "add-key") +
        " --key_pair_name foo --key_file_path " + TMP_KEYS_PATH + "/" + defaultProvider.uuid);
    expectedCommands.add(getBaseCommand(defaultRegion, "create-vault") +
        " --private_key_file " + TMP_KEYS_PATH + "/private.key");

    List<ArrayList> executedCommands = command.getAllValues();

    for (int idx=0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }

    cloudCredentials.getAllValues().forEach((cloudCredential) -> {
      assertEquals(config, cloudCredential);
    });
    assertValidAccessKey(json);
  }

  @Test
  public void testManageAddKeyCommandWithErrorResponse() {
    try {
      runCommand(defaultRegion.uuid, "add-key", true);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("AccessManager failed to execute")));
    }
    Mockito.verify(shellProcessHandler, times(1)).run(anyList(), anyMap());
  }

  @Test
  public void testManageAddKeyExistingKeyCode() {
    AccessKey.KeyInfo keyInfo =  new AccessKey.KeyInfo();
    keyInfo.privateKey = TMP_KEYS_PATH + "/private.key";
    AccessKey.create(defaultProvider.uuid, "foo", keyInfo);
    runCommand(defaultRegion.uuid, "add-key", false);
    Mockito.verify(shellProcessHandler, times(1)).run((List<String>) command.capture(),
        (Map<String, String>) cloudCredentials.capture());
    String expectedCommand = getBaseCommand(defaultRegion, "add-key") +
        " --key_pair_name foo --key_file_path " + TMP_KEYS_PATH + "/" +
        defaultProvider.uuid + " --private_key_file " + keyInfo.privateKey;
    assertEquals(String.join(" ", command.getValue()), expectedCommand);
  }

  private void assertValidAccessKey(JsonNode json) {
    JsonNode idKey = json.get("idKey");
    assertNotNull(idKey);
    AccessKey accessKey = AccessKey.get(
        UUID.fromString(idKey.get("providerUUID").asText()),
        idKey.get("keyCode").asText());
    assertNotNull(accessKey);
    JsonNode keyInfo = Json.toJson(accessKey.getKeyInfo());
    assertValue(keyInfo, "publicKey", TMP_KEYS_PATH + "/public.key");
    assertValue(keyInfo, "privateKey", TMP_KEYS_PATH + "/private.key");
    assertValue(keyInfo, "vaultFile", "/path/to/vault_file");
    assertValue(keyInfo, "vaultPasswordFile", "/path/to/vault_password");
  }

  @Test
  public void testManageListKeysCommand() {
    JsonNode result = runCommand(defaultRegion.uuid, "list-keys", false);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(),
        cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultRegion, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(cloudCredentials.getValue().isEmpty());
    assertValue(result, "foo", "bar");
  }

  @Test
  public void testManageListKeysCommandWithErrorResponse() {
    JsonNode result = runCommand(defaultRegion.uuid, "list-keys", true);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(), anyMap());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultRegion, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertValue(result, "error", "AccessManager failed to execute");
  }

  @Test
  public void testManageUploadKeyFile() {
    JsonNode result = uploadKeyCommand(defaultRegion.uuid, false);
    JsonNode idKey = result.get("idKey");
    assertNotNull(idKey);
    AccessKey accessKey = AccessKey.get(
        UUID.fromString(idKey.get("providerUUID").asText()),
        idKey.get("keyCode").asText());
    assertNotNull(accessKey);
    assertEquals("test-key.pem", accessKey.getKeyInfo().privateKey);
  }

  @Test
  public void testManageUploadKeyFileError() {
    try {
      uploadKeyCommand(defaultRegion.uuid, true);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("Key file foo not found.")));
    }
  }

  @Test
  public void testManageUploadKeyDuplicateKeyCode() {
    AccessKey.KeyInfo keyInfo =  new AccessKey.KeyInfo();
    keyInfo.privateKey = TMP_KEYS_PATH + "/private.key";
    AccessKey.create(defaultProvider.uuid, "test-key", keyInfo);
    try {
      uploadKeyCommand(defaultRegion.uuid, false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("Duplicate Access KeyCode: test-key")));
    }
  }

  @Test
  public void testManageUploadKeyExistingKeyFile() throws IOException {
    String providerKeysPath = "keys/"  + defaultProvider.uuid;
    new File(TMP_KEYS_PATH, providerKeysPath).mkdirs();
    createTempFile(providerKeysPath + "/test-key.pem", "PRIVATE_KEY_FILE");

    try {
      uploadKeyCommand(defaultRegion.uuid, false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("File test-key.pem already exists.")));
    }
  }

  @Test
  public void testCreateVaultWithInvalidFile() {
    File file = new File(TMP_KEYS_PATH + "/vault-private.key");
    file.delete();
    try {
      runCommand(defaultRegion.uuid, "create-vault", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
          equalTo("File " + TMP_KEYS_PATH + "/vault-private.key doesn't exists.")));
    }
  }

  @Test
  public void testCreateVaultWithValidFile() {
    createTempFile("keys/vault-private.key", "PRIVATE_KEY_FILE");
    JsonNode result = runCommand(defaultRegion.uuid, "create-vault", false);
    Mockito.verify(shellProcessHandler, times(1)).run(command.capture(),
        cloudCredentials.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultRegion, "create-vault") +
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
      runCommand(defaultRegion.uuid, "add-key", false);
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
      runCommand(defaultRegion.uuid, "add-key", false);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Key path /foo/keys doesn't exists.")));
    }
  }

  @Test
  public void testExecuteAgainstRegionWithoutZones() {
    Region region = Region.create(defaultProvider, "new-region", "new region", "yb-image");
    JsonNode result = runCommand(region.uuid, "list-keys", true);
    Mockito.verify(shellProcessHandler, times(0)).run(anyList(), anyMap());
    assertErrorNodeValue(result, "No Zones found for Region UUID: " + region.uuid);
  }
}
