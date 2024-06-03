// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.Duration;
import java.util.*;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AccessManagerTest extends FakeDBApplication {

  @InjectMocks AccessManager accessManager;

  @Mock ShellProcessHandler shellProcessHandler;

  @Mock Config mockConfig;

  @Mock RuntimeConfGetter mockConfGetter;

  private Provider defaultProvider;
  private Region defaultRegion;
  private Customer defaultCustomer;
  ArgumentCaptor<List<String>> command;
  ArgumentCaptor<ShellProcessContext> shellProcessContext;

  static final String TMP_STORAGE_PATH = "/tmp/yugaware_tests/amt";
  static final String TMP_KEYS_PATH = TMP_STORAGE_PATH + "/keys";
  static final String TEST_KEY_CODE = "test-key";
  static final String TEST_KEY_CODE_WITH_PATH = "../../test-key";
  static final String TEST_KEY_PEM = TEST_KEY_CODE + ".pem";
  static final String PEM_PERMISSIONS = "r--------";
  static final Integer SSH_PORT = 12345;

  @Before
  public void beforeTest() {
    new File(TMP_KEYS_PATH).mkdirs();
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider, "us-west-2", "US West 2", "yb-image");
    when(mockConfig.getString("yb.storage.path")).thenReturn(TMP_STORAGE_PATH);
    command = ArgumentCaptor.forClass(List.class);
    shellProcessContext = ArgumentCaptor.forClass(ShellProcessContext.class);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  private JsonNode uploadKeyCommand(UUID regionUUID, String keyCode, boolean mimicError)
      throws IOException {
    String tmpVaultFile =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_file";
    String tmpVaultPassword =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_password";
    createTempFile(
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
        "vault_file",
        "TEST");
    createTempFile(
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
        "vault_password",
        "TEST");

    ShellResponse response = new ShellResponse();
    if (mimicError) {
      response.message = "Unknown error occurred";
      response.code = 99;
      return Json.toJson(
          accessManager.uploadKeyFile(
              regionUUID,
              new File("foo").toPath(),
              keyCode,
              AccessManager.KeyType.PRIVATE,
              "some-user",
              SSH_PORT,
              false,
              false,
              false,
              null,
              false));
    } else {
      response.code = 0;
      response.message =
          "{\"vault_file\": \""
              + tmpVaultFile
              + "\","
              + "\"vault_password\": \""
              + tmpVaultPassword
              + "\"}";
      when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
      String tmpFile = createTempFile("SOME DATA");
      return Json.toJson(
          accessManager.uploadKeyFile(
              regionUUID,
              new File(tmpFile).toPath(),
              keyCode,
              AccessManager.KeyType.PRIVATE,
              "some-user",
              SSH_PORT,
              false,
              false,
              false,
              null,
              false));
    }
  }

  private JsonNode runCommand(UUID regionUUID, String commandType, boolean mimicError) {
    String tmpVaultFile =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_file";
    String tmpVaultPassword =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_password";
    createTempFile(
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
        "vault_file",
        "TEST");
    createTempFile(
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
        "vault_password",
        "TEST");

    ShellResponse response = new ShellResponse();
    if (mimicError) {
      response.message = "Unknown error occurred";
      response.code = 99;
      when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
    } else {
      response.code = 0;
      if (commandType.equals("add-key")) {
        String tmpPrivateFile =
            TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/private.key";
        createTempFile(
            TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
            "private.key",
            "PRIVATE_KEY_FILE");
        String tmpPublicFile =
            TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/public.key";
        createTempFile(
            TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
            "public.key",
            "PUBLIC_KEY_FILE");
        response.message =
            "{\"public_key\":\""
                + tmpPublicFile
                + "\" ,"
                + "\"private_key\": \""
                + tmpPrivateFile
                + "\"}";
        // In case of add-key we make two calls via shellProcessHandler one to add the key,
        // and other call to create a vault file for the keys generated.
        ShellResponse response2 = new ShellResponse();
        response2.code = 0;
        response2.message =
            "{\"vault_file\": \""
                + tmpVaultFile
                + "\","
                + "\"vault_password\": \""
                + tmpVaultPassword
                + "\"}";
        when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
            .thenReturn(response)
            .thenReturn(response2);
      } else {
        if (commandType.equals("create-vault")) {
          response.message =
              "{\"vault_file\": \""
                  + tmpVaultFile
                  + "\","
                  + "\"vault_password\": \""
                  + tmpVaultPassword
                  + "\"}";
        } else {
          response.message = "{\"foo\": \"bar\"}";
        }
        when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
            .thenReturn(response);
      }
    }

    switch (commandType) {
      case "add-key":
        return Json.toJson(
            accessManager.addKey(regionUUID, "foo", SSH_PORT, false, false, false, null, false));
      case "list-keys":
        return accessManager.listKeys(regionUUID);
      case "create-vault":
        String tmpPrivateFile =
            TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/private.key";
        return accessManager.createVault(regionUUID, tmpPrivateFile);
      case "delete-key":
        return accessManager.deleteKey(regionUUID, "foo");
    }
    return null;
  }

  private String getBaseCommand(Region region, String commandType) {
    return "bin/ybcloud.sh "
        + region.getProviderCloudCode()
        + " --region "
        + region.getCode()
        + " access "
        + commandType;
  }

  @Test
  public void testManageAddKeyCommandWithoutProviderConfig() {
    JsonNode json = runCommand(defaultRegion.getUuid(), "add-key", false);
    Mockito.verify(shellProcessHandler, times(2))
        .run(command.capture(), shellProcessContext.capture());

    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(
        getBaseCommand(defaultRegion, "add-key")
            + " --key_pair_name foo --key_file_path "
            + TMP_KEYS_PATH
            + File.separator
            + defaultProvider.getUuid());
    expectedCommands.add(
        getBaseCommand(defaultRegion, "create-vault")
            + " --private_key_file "
            + TMP_KEYS_PATH
            + File.separator
            + defaultProvider.getUuid()
            + "/private.key");

    List<List<String>> executedCommands = command.getAllValues();
    for (int idx = 0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }
    shellProcessContext.getAllValues().stream()
        .map(ShellProcessContext::getExtraEnvVars)
        .forEach((cloudCredential) -> assertTrue(cloudCredential.isEmpty()));
    assertValidAccessKey(json);
    assertEquals(4, FileData.getCount());
  }

  @Test
  public void testManageAddKeyCommandWithProviderConfig() {
    Map<String, String> config = new HashMap<>();
    config.put("AWS_ACCESS_KEY_ID", "ACCESS-KEY");
    config.put("AWS_SECRET_ACCESS_KEY", "ACCESS-SECRET");
    CloudInfoInterface.setCloudProviderInfoFromConfig(defaultProvider, config);
    defaultProvider.save();

    createTempFile(TMP_KEYS_PATH, "private.key", "test data");

    JsonNode json = runCommand(defaultRegion.getUuid(), "add-key", false);
    Mockito.verify(shellProcessHandler, times(2))
        .run(command.capture(), shellProcessContext.capture());
    List<String> expectedCommands = new ArrayList<>();
    expectedCommands.add(
        getBaseCommand(defaultRegion, "add-key")
            + " --key_pair_name foo --key_file_path "
            + TMP_KEYS_PATH
            + "/"
            + defaultProvider.getUuid());
    expectedCommands.add(
        getBaseCommand(defaultRegion, "create-vault")
            + " --private_key_file "
            + TMP_KEYS_PATH
            + File.separator
            + defaultProvider.getUuid()
            + "/private.key");

    List<List<String>> executedCommands = command.getAllValues();

    for (int idx = 0; idx < executedCommands.size(); idx++) {
      String executedCommand = String.join(" ", executedCommands.get(idx));
      assertThat(expectedCommands.get(idx), allOf(notNullValue(), equalTo(executedCommand)));
    }

    shellProcessContext.getAllValues().stream()
        .map(ShellProcessContext::getExtraEnvVars)
        .forEach((cloudCredential) -> assertEquals(config, cloudCredential));
    assertValidAccessKey(json);

    assertEquals(4, FileData.getCount());
  }

  @Test
  public void testManageAddKeyCommandWithErrorResponse() {
    try {
      runCommand(defaultRegion.getUuid(), "add-key", true);
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(),
          allOf(
              notNullValue(),
              equalTo(
                  "Parsing of Region failed with :"
                      + " YBCloud command access (add-key) failed to execute."
                      + " Unknown error occurred")));
    }
    Mockito.verify(shellProcessHandler, times(1)).run(anyList(), any(ShellProcessContext.class));
  }

  @Test
  public void testManageAddKeyExistingKeyCode() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = TMP_KEYS_PATH + "/private.key";
    AccessKey.create(defaultProvider.getUuid(), "foo", keyInfo);
    runCommand(defaultRegion.getUuid(), "add-key", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), shellProcessContext.capture());
    String expectedCommand =
        getBaseCommand(defaultRegion, "add-key")
            + " --key_pair_name foo --key_file_path "
            + TMP_KEYS_PATH
            + "/"
            + defaultProvider.getUuid()
            + " --private_key_file "
            + keyInfo.privateKey;
    assertEquals(String.join(" ", command.getValue()), expectedCommand);
  }

  private void assertValidAccessKey(JsonNode json) {
    String tmpVaultFile =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_file";
    String tmpVaultPassword =
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/vault_password";
    JsonNode idKey = json.get("idKey");
    assertNotNull(idKey);
    AccessKey accessKey =
        AccessKey.get(
            UUID.fromString(idKey.get("providerUUID").asText()), idKey.get("keyCode").asText());
    assertNotNull(accessKey);
    JsonNode keyInfo = Json.toJson(accessKey.getKeyInfo());
    assertValue(
        keyInfo,
        "publicKey",
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/public.key");
    assertValue(
        keyInfo,
        "privateKey",
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/private.key");
    assertValue(keyInfo, "vaultFile", tmpVaultFile);
    assertValue(keyInfo, "vaultPasswordFile", tmpVaultPassword);
    assertEquals(
        keyInfo.get("managementState").textValue(),
        AccessKey.KeyInfo.KeyManagementState.YBAManaged.toString());
  }

  @Test
  public void testManageListKeysCommand() {
    JsonNode result = runCommand(defaultRegion.getUuid(), "list-keys", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), shellProcessContext.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultRegion, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(shellProcessContext.getValue().getExtraEnvVars().isEmpty());
    assertValue(result, "foo", "bar");
  }

  @Test
  public void testManageListKeysCommandWithErrorResponse() {
    JsonNode result = runCommand(defaultRegion.getUuid(), "list-keys", true);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), any(ShellProcessContext.class));

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd = getBaseCommand(defaultRegion, "list-keys");
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertValue(
        result,
        "error",
        "YBCloud command access (list-keys) failed to execute. Unknown error occurred");
  }

  @Test
  public void testManageUploadKeyFile_PureKeyCode() throws IOException {
    doTestManageUploadKeyFile(TEST_KEY_CODE, TEST_KEY_PEM);
  }

  @Test
  public void testManageUploadKeyFile_KeyCodeWithPath() throws IOException {
    doTestManageUploadKeyFile(TEST_KEY_CODE_WITH_PATH, TEST_KEY_PEM);
  }

  private void doTestManageUploadKeyFile(String keyCode, String expectedFilename)
      throws IOException {
    JsonNode result = uploadKeyCommand(defaultRegion.getUuid(), keyCode, false);
    JsonNode idKey = result.get("idKey");
    assertNotNull(idKey);
    AccessKey accessKey =
        AccessKey.get(
            UUID.fromString(idKey.get("providerUUID").asText()), idKey.get("keyCode").asText());
    assertNotNull(accessKey);
    assertEquals(accessKey.getKeyCode() + ".pem", expectedFilename);
    String expectedPath =
        String.join("/", TMP_KEYS_PATH, idKey.get("providerUUID").asText(), expectedFilename);
    assertEquals(expectedPath, accessKey.getKeyInfo().privateKey);
    assertEquals(
        accessKey.getKeyInfo().getManagementState(),
        AccessKey.KeyInfo.KeyManagementState.SelfManaged);
    defaultProvider.refresh();
    assertEquals("some-user", defaultProvider.getDetails().sshUser);
    Path keyFile = Paths.get(expectedPath);
    String permissions = PosixFilePermissions.toString(Files.getPosixFilePermissions(keyFile));
    assertEquals(PEM_PERMISSIONS, permissions);
    assertEquals(3, FileData.getCount());
  }

  @Test
  public void testManageUploadKeyFileError() throws IOException {
    try {
      uploadKeyCommand(defaultRegion.getUuid(), TEST_KEY_CODE, true);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), equalTo("Key file foo not found.")));
    }
  }

  @Test
  public void testManageUploadKeyDuplicateKeyCode_PureKeyCode() throws IOException {
    doTestManageUploadKeyDuplicateKeyCode();
  }

  @Test
  public void testManageUploadKeyDuplicateKeyCode_KeyCodeWithPath() throws IOException {
    doTestManageUploadKeyDuplicateKeyCode();
  }

  private void doTestManageUploadKeyDuplicateKeyCode() throws IOException {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = TMP_KEYS_PATH + "/private.key";
    AccessKey.create(defaultProvider.getUuid(), TEST_KEY_CODE, keyInfo);
    try {
      uploadKeyCommand(defaultRegion.getUuid(), TEST_KEY_CODE, false);
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(),
          allOf(notNullValue(), equalTo("Duplicate Access KeyCode: " + TEST_KEY_CODE)));
    }
  }

  @Test
  public void testManageUploadKeyExistingKeyFile_PureKeyCode() throws IOException {
    doTestManageUploadKeyExistingKeyFile(TEST_KEY_CODE, TEST_KEY_PEM);
  }

  @Test
  public void testManageUploadKeyExistingKeyFile_KeyCodeWithPath() throws IOException {
    doTestManageUploadKeyExistingKeyFile(TEST_KEY_CODE_WITH_PATH, TEST_KEY_PEM);
  }

  private void doTestManageUploadKeyExistingKeyFile(String keyCode, String expectedFilename)
      throws IOException {
    String providerKeysPath = "keys/" + defaultProvider.getUuid();
    new File(TMP_KEYS_PATH, providerKeysPath).mkdirs();
    createTempFile(providerKeysPath + "/" + expectedFilename, "PRIVATE_KEY_FILE");

    try {
      uploadKeyCommand(defaultRegion.getUuid(), keyCode, false);
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(),
          allOf(notNullValue(), equalTo("File " + expectedFilename + " already exists.")));
    }
  }

  @Test
  public void testCreateVaultWithInvalidFile() {
    File file =
        new File(TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + "/private.key");
    file.delete();
    try {
      runCommand(defaultRegion.getUuid(), "create-vault", false);
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(),
          allOf(
              notNullValue(),
              equalTo(
                  "File "
                      + TMP_KEYS_PATH
                      + File.separator
                      + defaultProvider.getUuid()
                      + "/private.key doesn't exists.")));
    }
  }

  @Test
  public void testCreateVaultWithValidFile() {
    String tmpVaultFile = TMP_KEYS_PATH + "/" + defaultProvider.getUuid() + "/vault_file";
    String tmpVaultPassword = TMP_KEYS_PATH + "/" + defaultProvider.getUuid() + "/vault_password";
    createTempFile(
        TMP_KEYS_PATH + File.separator + defaultProvider.getUuid() + File.separator,
        "private.key",
        "TEST");
    JsonNode result = runCommand(defaultRegion.getUuid(), "create-vault", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), shellProcessContext.capture());

    String commandStr = String.join(" ", command.getValue());
    String expectedCmd =
        getBaseCommand(defaultRegion, "create-vault")
            + " --private_key_file "
            + TMP_KEYS_PATH
            + File.separator
            + defaultProvider.getUuid()
            + "/private.key";
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(shellProcessContext.getValue().getExtraEnvVars().isEmpty());
    assertValue(result, "vault_file", tmpVaultFile);
    assertValue(result, "vault_password", tmpVaultPassword);
  }

  @Test
  public void testKeysBasePathCreateFailure() {
    createTempFile(TMP_KEYS_PATH, defaultProvider.getUuid().toString(), "RANDOM DATA");

    Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    String tmpFilePath = TMP_KEYS_PATH + "/" + defaultProvider.getUuid();
    assertThat(
        () -> runCommand(defaultRegion.getUuid(), "add-key", false),
        thrown(RuntimeException.class, "Unable to create key file path " + tmpFilePath));
  }

  @Test
  public void testInvalidKeysBasePath() {
    when(mockConfig.getString("yb.storage.path")).thenReturn("/sys/foo");
    Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    RuntimeException re =
        assertThrows(
            RuntimeException.class, () -> runCommand(defaultRegion.getUuid(), "add-key", false));
    assertThat(
        re.getMessage(), allOf(notNullValue(), equalTo("Key path /sys/foo/keys doesn't exist.")));
  }

  @Test
  public void testDeleteKeyWithInvalidRegion() {
    UUID regionUUID = UUID.randomUUID();
    try {
      runCommand(regionUUID, "delete-key", false);
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(), allOf(notNullValue(), equalTo("Invalid Region UUID: " + regionUUID)));
    }
  }

  @Test
  public void testDeleteKeyWithValidRegion() {
    JsonNode result = runCommand(defaultRegion.getUuid(), "delete-key", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), shellProcessContext.capture());
    String expectedCmd =
        getBaseCommand(defaultRegion, "delete-key")
            + " --key_pair_name foo --key_file_path "
            + TMP_KEYS_PATH
            + "/"
            + defaultProvider.getUuid()
            + " --delete_remote"
            + " --ignore_auth_failure";
    String commandStr = String.join(" ", command.getValue());
    assertThat(commandStr, allOf(notNullValue(), equalTo(expectedCmd)));
    assertTrue(shellProcessContext.getValue().getExtraEnvVars().isEmpty());
    assertValue(result, "foo", "bar");
  }

  @Test
  public void testDeleteKeyWithValidRegionInGCP() {
    Provider testProvider = ModelFactory.gcpProvider(defaultCustomer);
    Region testRegion = Region.create(testProvider, "us-west-2", "US West 2", "yb-image");
    runCommand(testRegion.getUuid(), "delete-key", false);
    Mockito.verify(shellProcessHandler, times(1))
        .run(command.capture(), shellProcessContext.capture());
  }

  @Test
  public void testDeleteKeyWithErrorResponse() {
    try {
      runCommand(defaultRegion.getUuid(), "delete-key", true);
      Mockito.verify(shellProcessHandler, times(0)).run(command.capture(), anyMap());
    } catch (RuntimeException re) {
      assertThat(
          re.getMessage(),
          allOf(
              notNullValue(),
              equalTo(
                  "YBCloud command access (delete-key) failed to execute."
                      + " Unknown error occurred")));
    }
  }

  @Test
  public void testCreateKubernetesConfig_PureConfigName() {
    doTestCreateKubernetesConfig("demo.cnf");
  }

  @Test
  public void testCreateKubernetesConfig_ConfigNameWithPath() {
    doTestCreateKubernetesConfig("../../demo.cnf");
  }

  void doTestCreateKubernetesConfig(String configName) {
    try {
      Map<String, String> config = new HashMap<>();
      config.put("KUBECONFIG_NAME", configName);
      config.put("KUBECONFIG_CONTENT", "hello world");
      String configFile =
          accessManager.createKubernetesConfig(defaultProvider.getUuid().toString(), config, false);
      assertEquals(
          "/tmp/yugaware_tests/amt/keys/"
              + defaultProvider.getUuid()
              + "/"
              + com.yugabyte.yw.common.utils.FileUtils.getFileName(configName),
          configFile);
      List<String> lines = Files.readAllLines(Paths.get(configFile));
      Set<PosixFilePermission> filePermissions =
          Files.getPosixFilePermissions(Paths.get(configFile));
      assertEquals("hello world", lines.get(0));
      assertNull(config.get("KUBECONFIG_NAME"));
      assertNull(config.get("KUBECONFIG_CONTENT"));
      assertEquals(filePermissions, PosixFilePermissions.fromString("rw-------"));
      assertEquals(1, FileData.getCount());
    } catch (IOException e) {
      e.printStackTrace();
      assertNotNull(e.getMessage());
    }
  }

  @Test
  public void testCreateKubernetesConfigMissingConfig() {
    try {
      Map<String, String> config = new HashMap<>();
      accessManager.createKubernetesConfig(defaultProvider.getUuid().toString(), config, false);
    } catch (RuntimeException e) {
      assertEquals("Missing KUBECONFIG_NAME data in the provider config.", e.getMessage());
    }
  }

  @Test
  public void testCreateKubernetesConfigFileExists() {
    String path, providerPath;
    try {
      Map<String, String> config = new HashMap<>();
      providerPath = TMP_KEYS_PATH + "/" + defaultProvider.getUuid();
      Files.createDirectory(Paths.get(providerPath));
      Files.write(Paths.get(providerPath + "/demo.conf"), ImmutableList.of("hello world"));
      config.put("KUBECONFIG_NAME", "demo.conf");
      config.put("KUBECONFIG_CONTENT", "hello world");
      path =
          accessManager.createKubernetesConfig(defaultProvider.getUuid().toString(), config, false);
    } catch (IOException | RuntimeException e) {
      assertEquals("File demo.conf already exists.", e.getMessage());
      return;
    }
    fail(
        String.format(
            "%s not expected to be created as it already exits (%s)", path, providerPath));
  }

  @Test
  public void testCreateCredentialsFile() throws IOException {
    ObjectMapper mapper = Json.mapper();
    ObjectNode credentials = Json.newObject();
    credentials.put("foo", "bar");
    credentials.put("hello", "world");
    String configFile =
        accessManager.createGCPCredentialsFile(
            defaultProvider.getUuid(), mapper.writeValueAsString(credentials));
    assertEquals(
        "/tmp/yugaware_tests/amt/keys/" + defaultProvider.getUuid() + "/credentials.json",
        configFile);
    List<String> lines = Files.readAllLines(Paths.get(configFile));
    assertEquals("{\"foo\":\"bar\",\"hello\":\"world\"}", lines.get(0));
    assertEquals(1, FileData.getCount());
  }

  @Test
  public void testReadCredentialsFromFile() throws IOException {
    ObjectMapper mapper = Json.mapper();
    Map<String, String> inputConfig = new HashMap<>();
    inputConfig.put("foo", "bar");
    inputConfig.put("hello", "world");
    accessManager.createGCPCredentialsFile(
        defaultProvider.getUuid(), mapper.writeValueAsString(inputConfig));
    Map<String, String> configMap =
        accessManager.readCredentialsFromFile(defaultProvider.getUuid());
    assertEquals(inputConfig, configMap);
  }
}
