// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.INTERNAL_SERVER_ERROR;
import static play.test.Helpers.contentAsString;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class AccessKeyControllerTest extends FakeDBApplication {

  Provider defaultProvider;
  Customer defaultCustomer;
  Users defaultUser;
  Region defaultRegion;

  static final Integer SSH_PORT = 12345;
  static final String DEFAULT_SUDO_SSH_USER = "ssh-user";

  @Before
  public void before() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultProvider = ModelFactory.onpremProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider, "us-west-2", "us-west-2", "yb-image");
  }

  private Result getAccessKey(UUID providerUUID, String keyCode) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/access_keys/"
            + keyCode;
    return doRequestWithAuthToken("GET", uri, defaultUser.createAuthToken());
  }

  private Result listAccessKey(UUID providerUUID) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/access_keys";
    return doRequestWithAuthToken("GET", uri, defaultUser.createAuthToken());
  }

  private Result listAccessKeyForAllProviders() {
    String uri = "/api/customers/" + defaultCustomer.getUuid() + "/access_keys";
    return doRequestWithAuthToken("GET", uri, defaultUser.createAuthToken());
  }

  private Result createAccessKey(
      UUID providerUUID, String keyCode, boolean uploadFile, boolean useRawString) {
    return createAccessKey(
        providerUUID,
        keyCode,
        uploadFile,
        useRawString,
        defaultRegion,
        true,
        true,
        false,
        false,
        Collections.emptyList());
  }

  private Result createAccessKey(
      UUID providerUUID,
      String keyCode,
      boolean uploadFile,
      boolean useRawString,
      Region region,
      boolean airGapInstall,
      boolean passwordlessSudoAccess,
      boolean skipProvisioning,
      boolean setUpChrony,
      List<String> ntpServers) {
    return createAccessKey(
        providerUUID,
        keyCode,
        uploadFile,
        useRawString,
        region,
        airGapInstall,
        passwordlessSudoAccess,
        skipProvisioning,
        setUpChrony,
        ntpServers,
        true,
        null);
  }

  private Result createAccessKey(
      UUID providerUUID,
      String keyCode,
      boolean uploadFile,
      boolean useRawString,
      Region region,
      boolean airGapInstall,
      boolean passwordlessSudoAccess,
      boolean skipProvisioning,
      boolean setUpChrony,
      List<String> ntpServers,
      boolean showSetUpChrony,
      Integer expirationThresholdDays) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/access_keys";

    if (uploadFile) {
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
      if (keyCode != null) {
        bodyData.add(new Http.MultipartFormData.DataPart("keyCode", keyCode));
        bodyData.add(
            new Http.MultipartFormData.DataPart("regionUUID", region.getUuid().toString()));
        bodyData.add(new Http.MultipartFormData.DataPart("keyType", "PRIVATE"));
        bodyData.add(new Http.MultipartFormData.DataPart("sshUser", "ssh-user"));
        bodyData.add(new Http.MultipartFormData.DataPart("sshPort", SSH_PORT.toString()));
        bodyData.add(new Http.MultipartFormData.DataPart("airGapInstall", "false"));
        String tmpFile = createTempFile("PRIVATE KEY DATA");
        Source<ByteString, ?> keyFile = FileIO.fromFile(new File(tmpFile));
        bodyData.add(
            new Http.MultipartFormData.FilePart<>(
                "keyFile", "test.pem", "application/octet-stream", keyFile));
      }

      return doRequestWithAuthTokenAndMultipartData(
          "POST", uri, defaultUser.createAuthToken(), bodyData, mat);
    } else {
      ObjectNode bodyJson = Json.newObject();
      if (keyCode != null) {
        bodyJson.put("keyCode", keyCode);
        bodyJson.put("regionUUID", region.getUuid().toString());
      }
      if (useRawString) {
        bodyJson.put("keyType", AccessManager.KeyType.PRIVATE.toString());
        bodyJson.put("keyContent", "PRIVATE KEY DATA");
      }
      bodyJson.put("airGapInstall", airGapInstall);
      bodyJson.put("sshUser", DEFAULT_SUDO_SSH_USER);
      bodyJson.put("sshPort", SSH_PORT);
      bodyJson.put("passwordlessSudoAccess", passwordlessSudoAccess);
      bodyJson.put("skipProvisioning", skipProvisioning);
      bodyJson.put("setUpChrony", setUpChrony);
      bodyJson.put("showSetUpChrony", showSetUpChrony);
      bodyJson.put("expirationThresholdDays", expirationThresholdDays);
      if (ntpServers != null) {
        ArrayNode arrayNode = Json.newArray();
        for (String server : ntpServers) {
          arrayNode.add(server);
        }
        bodyJson.putArray("ntpServers").addAll(arrayNode);
      }
      return doRequestWithAuthTokenAndBody("POST", uri, defaultUser.createAuthToken(), bodyJson);
    }
  }

  private Result deleteAccessKey(UUID providerUUID, String keyCode) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/access_keys/"
            + keyCode;
    return doRequestWithAuthToken("DELETE", uri, defaultUser.createAuthToken());
  }

  @Test
  public void testGetAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> getAccessKey(invalidProviderUUID, "foo"));
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testGetAccessKeyWithInvalidKeyCode() {
    Result result = assertPlatformException(() -> getAccessKey(defaultProvider.getUuid(), "foo"));
    assertEquals(BAD_REQUEST, result.status());
    assertBadRequest(result, "KeyCode not found: foo");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testGetAccessKeyWithValidKeyCode() {
    AccessKey accessKey =
        AccessKey.create(defaultProvider.getUuid(), "foo", new AccessKey.KeyInfo());
    Result result = getAccessKey(defaultProvider.getUuid(), accessKey.getKeyCode());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    JsonNode idKey = json.get("idKey");
    assertValue(idKey, "keyCode", accessKey.getKeyCode());
    assertValue(idKey, "providerUUID", accessKey.getProviderUUID().toString());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> listAccessKey(invalidProviderUUID));
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListAccessKeyWithEmptyData() {
    Result result = listAccessKey(defaultProvider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 0);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListAccessKeyWithValidData() {
    AccessKey.create(defaultProvider.getUuid(), "key-1", new AccessKey.KeyInfo());
    AccessKey.create(defaultProvider.getUuid(), "key-2", new AccessKey.KeyInfo());
    Result result = listAccessKey(defaultProvider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 2);
    json.forEach(
        (key) -> {
          assertThat(
              key.get("idKey").get("keyCode").asText(),
              allOf(notNullValue(), containsString("key-")));
          assertThat(
              key.get("idKey").get("providerUUID").asText(),
              allOf(notNullValue(), equalTo(defaultProvider.getUuid().toString())));
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListForAllProvidersAccessKeyWithEmptyData() {
    Result result = listAccessKeyForAllProviders();
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 0);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListForAllProvidersAccessKeyWithValidData() {
    Provider newProvider = ModelFactory.gcpProvider(defaultCustomer);
    AccessKey.create(defaultProvider.getUuid(), "key-1", new AccessKey.KeyInfo());
    AccessKey.create(defaultProvider.getUuid(), "key-2", new AccessKey.KeyInfo());
    AccessKey.create(newProvider.getUuid(), "key-3", new AccessKey.KeyInfo());
    Result result = listAccessKeyForAllProviders();
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 3);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithInvalidProviderUUID() {
    Result result =
        assertPlatformException(() -> createAccessKey(UUID.randomUUID(), "foo", false, false));
    assertBadRequest(result, "provider");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithInvalidParams() {
    Result result =
        assertPlatformException(
            () -> createAccessKey(defaultProvider.getUuid(), null, false, false));
    JsonNode node = Json.parse(contentAsString(result));
    assertErrorNodeValue(node, "keyCode", "This field is required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithZewroRegionProvider() {
    Provider differentProvider =
        ModelFactory.onpremProvider(ModelFactory.testCustomer("fb", "foo@bar.com"));
    Result result =
        assertPlatformException(
            () -> createAccessKey(differentProvider.getUuid(), "key-code", false, false));
    assertInternalServerError(result, "No regions.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyInDifferentRegion() {
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            true,
            false,
            false,
            Collections.emptyList(),
            true))
        .thenAnswer(
            invocation ->
                AccessKey.create(defaultProvider.getUuid(), "key-code", new AccessKey.KeyInfo()));
    Result result = createAccessKey(defaultProvider.getUuid(), "key-code", false, false);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertValue(json.get("idKey"), "keyCode", "key-code");
    assertValue(json.get("idKey"), "providerUUID", defaultProvider.getUuid().toString());
  }

  @Test
  public void testCreateAccessKeyWithoutKeyFile() {
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            true,
            false,
            false,
            Collections.emptyList(),
            true))
        .thenAnswer(i -> AccessKey.create(defaultProvider.getUuid(), "key-code-1", new KeyInfo()));
    Result result = createAccessKey(defaultProvider.getUuid(), "key-code-1", false, false);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertValue(json.get("idKey"), "keyCode", "key-code-1");
    assertValue(json.get("idKey"), "providerUUID", defaultProvider.getUuid().toString());
  }

  @Test
  public void testCreateAccessKeyWithKeyFile() throws IOException {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    ArgumentCaptor<Path> updatedFile = ArgumentCaptor.forClass(Path.class);
    when(mockAccessManager.uploadKeyFile(
            eq(defaultRegion.getUuid()),
            any(Path.class),
            eq("key-code-1"),
            eq(AccessManager.KeyType.PRIVATE),
            eq("ssh-user"),
            eq(SSH_PORT),
            eq(false),
            eq(false),
            eq(false),
            eq(Collections.emptyList()),
            eq(true),
            eq(true),
            eq(false)))
        .thenAnswer(i -> AccessKey.create(defaultProvider.getUuid(), "key-code-1", keyInfo));
    Result result = createAccessKey(defaultProvider.getUuid(), "key-code-1", true, false);
    verify(mockAccessManager, times(1))
        .uploadKeyFile(
            eq(defaultRegion.getUuid()),
            updatedFile.capture(),
            eq("key-code-1"),
            eq(AccessManager.KeyType.PRIVATE),
            eq("ssh-user"),
            eq(SSH_PORT),
            eq(false),
            eq(false),
            eq(false),
            eq(Collections.emptyList()),
            eq(true),
            eq(true),
            eq(false));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertNotNull(json);
    try {
      List<String> lines = Files.readAllLines(updatedFile.getValue());
      assertEquals(1, lines.size());
      assertThat(lines.get(0), allOf(notNullValue(), equalTo("PRIVATE KEY DATA")));
    } catch (IOException e) {
      assertNull(e.getMessage());
    }
  }

  @Test
  public void testCreateAccessKeyWithKeyString() throws IOException {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    ArgumentCaptor<Path> updatedFile = ArgumentCaptor.forClass(Path.class);
    when(mockAccessManager.uploadKeyFile(
            eq(defaultRegion.getUuid()),
            any(Path.class),
            eq("key-code-1"),
            eq(AccessManager.KeyType.PRIVATE),
            eq(DEFAULT_SUDO_SSH_USER),
            eq(SSH_PORT),
            eq(true),
            eq(false),
            eq(false),
            eq(Collections.emptyList()),
            eq(true),
            eq(true),
            eq(false)))
        .thenAnswer(i -> AccessKey.create(defaultProvider.getUuid(), "key-code-1", keyInfo));
    Result result = createAccessKey(defaultProvider.getUuid(), "key-code-1", false, true);
    verify(mockAccessManager, times(1))
        .uploadKeyFile(
            eq(defaultRegion.getUuid()),
            updatedFile.capture(),
            eq("key-code-1"),
            eq(AccessManager.KeyType.PRIVATE),
            eq(DEFAULT_SUDO_SSH_USER),
            eq(SSH_PORT),
            eq(true),
            eq(false),
            eq(false),
            eq(Collections.emptyList()),
            eq(true),
            eq(true),
            eq(false));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertNotNull(json);
    try {
      List<String> lines = Files.readAllLines(updatedFile.getValue());
      assertEquals(1, lines.size());
      assertThat(lines.get(0), allOf(notNullValue(), equalTo("PRIVATE KEY DATA")));
    } catch (IOException e) {
      assertNull(e.getMessage());
    }
  }

  @Test
  @Ignore
  public void testCreateAccessKeyWithException() {
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            true,
            false,
            false,
            Collections.emptyList(),
            true))
        .thenThrow(new PlatformServiceException(INTERNAL_SERVER_ERROR, "Something went wrong!!"));
    Result result =
        assertPlatformException(
            () -> createAccessKey(defaultProvider.getUuid(), "key-code-1", false, false));
    assertErrorResponse(result, "Something went wrong!!");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithoutPasswordlessSudoAccessSuccess() {
    Provider onpremProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.onprem);
    Region onpremRegion = Region.create(onpremProvider, "onprem-a", "onprem-a", "yb-image");
    when(mockAccessManager.addKey(
            onpremRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            true,
            false,
            false,
            Collections.emptyList(),
            true))
        .thenAnswer(
            i -> AccessKey.create(onpremProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));
    Result result =
        createAccessKey(
            onpremProvider.getUuid(),
            "key-code-1",
            false,
            false,
            onpremRegion,
            true,
            false,
            false,
            false,
            Collections.emptyList());
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    verify(mockTemplateManager, times(1))
        .createProvisionTemplate(
            AccessKey.getLatestKey(onpremProvider.getUuid()),
            true,
            false,
            true,
            9300,
            "prometheus",
            false,
            Collections.emptyList());
  }

  @Test
  public void testCreateAccessKeyWithoutPasswordlessSudoAccessError() {
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            false,
            false,
            false,
            Collections.emptyList(),
            true))
        .thenAnswer(
            i ->
                AccessKey.create(defaultProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));
    doThrow(
            new PlatformServiceException(
                INTERNAL_SERVER_ERROR, "Unable to create access key: key-code-1"))
        .when(mockTemplateManager)
        .createProvisionTemplate(
            any(AccessKey.class),
            eq(false),
            eq(false),
            eq(true),
            eq(9300),
            eq("prometheus"),
            eq(false),
            eq(Collections.emptyList()));
    Result result =
        assertPlatformException(
            () ->
                createAccessKey(
                    defaultProvider.getUuid(),
                    "key-code-1",
                    false,
                    false,
                    defaultRegion,
                    false,
                    false,
                    false,
                    false,
                    Collections.emptyList()));
    assertErrorResponse(result, "Unable to create access key: key-code-1");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeySkipProvisioning() {
    Provider onpremProvider = ModelFactory.newProvider(defaultCustomer, Common.CloudType.onprem);
    Region onpremRegion = Region.create(onpremProvider, "onprem-a", "onprem-a", "yb-image");
    when(mockAccessManager.addKey(
            onpremRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            true,
            true,
            false,
            Collections.emptyList(),
            true))
        .thenAnswer(
            invocation ->
                AccessKey.create(onpremProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));
    Result result =
        createAccessKey(
            onpremProvider.getUuid(),
            "key-code-1",
            false,
            false,
            onpremRegion,
            true,
            false,
            true,
            false,
            Collections.emptyList());
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    verify(mockTemplateManager, times(1))
        .createProvisionTemplate(
            AccessKey.getLatestKey(onpremProvider.getUuid()),
            true,
            false,
            true,
            9300,
            "prometheus",
            false,
            Collections.emptyList());
  }

  @Test
  public void testCreateAccessKeySetUpNTP() {
    List<String> serverList = new ArrayList<>();
    serverList.add("0.yb.pool.ntp.org");
    serverList.add("1.yb.pool.ntp.org");
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            false,
            false,
            true,
            serverList,
            true))
        .thenAnswer(
            i ->
                AccessKey.create(defaultProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));
    Result result =
        createAccessKey(
            defaultProvider.getUuid(),
            "key-code-1",
            false,
            false,
            defaultRegion,
            false,
            false,
            false,
            true,
            serverList);
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeySetUpNTPWithList() {
    List<String> serverList = new ArrayList<>();
    serverList.add("0.yb.pool.ntp.org");
    serverList.add("1.yb.pool.ntp.org");
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            false,
            false,
            true,
            serverList,
            true))
        .thenAnswer(
            i ->
                AccessKey.create(defaultProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));
    Result result =
        createAccessKey(
            defaultProvider.getUuid(),
            "key-code-1",
            false,
            false,
            defaultRegion,
            false,
            false,
            false,
            true,
            serverList);
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeySetUpNTPWithoutListOnPrem() {
    Result result =
        assertPlatformException(
            () ->
                createAccessKey(
                    defaultProvider.getUuid(),
                    "key-code-1",
                    false,
                    false,
                    defaultRegion,
                    false,
                    false,
                    false,
                    true,
                    Collections.emptyList()));
    assertBadRequest(result, "NTP servers not provided");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithShowSetUpChronyFalse() {
    List<String> ntpServerList = ImmutableList.of("0.yb.pool.ntp.org", "1.yb.pool.ntp.org");
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            "key-code-1",
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            false,
            false,
            true,
            ntpServerList,
            false))
        .then(
            invocation ->
                AccessKey.create(defaultProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo()));

    Result result =
        createAccessKey(
            defaultProvider.getUuid(),
            "key-code-1",
            false,
            false,
            defaultRegion,
            false,
            false,
            false,
            true,
            ntpServerList,
            false,
            null);
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAccessKeyWithExpirationThresholdDays() {
    Integer expirationThresholdDays = 365;
    final String keyCode = "key-code-1";
    when(mockAccessManager.addKey(
            defaultRegion.getUuid(),
            keyCode,
            null,
            DEFAULT_SUDO_SSH_USER,
            SSH_PORT,
            false,
            false,
            false,
            Collections.emptyList(),
            false))
        .thenAnswer(i -> AccessKey.create(defaultProvider.getUuid(), keyCode, new KeyInfo()));
    Result result =
        createAccessKey(
            defaultProvider.getUuid(),
            keyCode,
            false,
            false,
            defaultRegion,
            false,
            false,
            false,
            false,
            Collections.emptyList(),
            false,
            expirationThresholdDays);

    assertOk(result);

    JsonNode json = Json.parse(contentAsString(result));
    AccessKey createdAccessKey = Json.fromJson(json, AccessKey.class);
    assertEquals(
        AccessKey.getLatestKey(defaultProvider.getUuid()).getExpirationDate().toString(),
        createdAccessKey.getExpirationDate().toString());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteAccessKey(invalidProviderUUID, "foo"));
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteAccessKeyWithInvalidAccessKeyCode() {
    Result result =
        assertPlatformException(() -> deleteAccessKey(defaultProvider.getUuid(), "foo"));
    assertBadRequest(result, "KeyCode not found: foo");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteAccessKeyWithValidAccessKeyCode() {
    AccessKey.create(defaultProvider.getUuid(), "key-code-1", new AccessKey.KeyInfo());
    Result result = deleteAccessKey(defaultProvider.getUuid(), "key-code-1");
    assertEquals(OK, result.status());
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertThat(
        contentAsString(result),
        allOf(notNullValue(), containsString("Deleted KeyCode: key-code-1")));
  }
}
