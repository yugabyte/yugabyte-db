// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mockito;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class AccessKeyControllerTest extends WithApplication {
  Provider defaultProvider;
  Customer defaultCustomer;
  Region defaultRegion;
  AccessManager mockAccessManager;

  @Override
  protected Application provideApplication() {
    mockAccessManager = mock(AccessManager.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
        .build();
  }

  @Before
  public void before() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider, "us-west-2", "us-west-2", "yb-image");
  }

  private Result getAccessKey(UUID providerUUID, String keyCode) {
    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + providerUUID + "/access_keys/" + keyCode;
    return FakeApiHelper.doRequestWithAuthToken("GET", uri,
        defaultCustomer.createAuthToken());
  }

  private Result listAccessKey(UUID providerUUID) {
    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + providerUUID + "/access_keys";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
  }

  private Result createAccessKey(UUID providerUUID, String keyCode, boolean uploadFile) {
    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + providerUUID + "/access_keys";

    if (uploadFile) {
      List<Http.MultipartFormData.Part<Source<ByteString, ?>>> bodyData = new ArrayList<>();
      if (keyCode != null) {
        bodyData.add(new Http.MultipartFormData.DataPart("keyCode", keyCode));
        bodyData.add(new Http.MultipartFormData.DataPart("regionUUID", defaultRegion.uuid.toString()));
        bodyData.add(new Http.MultipartFormData.DataPart("keyType", "PRIVATE"));
        String tmpFile = createTempFile("PRIVATE KEY DATA");
        Source<ByteString, ?> keyFile =  FileIO.fromFile(new File(tmpFile));
        bodyData.add(new Http.MultipartFormData.FilePart("keyFile", "test.pem",
            "application/octet-stream", keyFile));
      }

      return FakeApiHelper.doRequestWithAuthTokenAndMultipartData("POST", uri,
          defaultCustomer.createAuthToken(),
          bodyData,
          mat);
    } else {
      ObjectNode bodyJson = Json.newObject();
      if (keyCode != null) {
        bodyJson.put("keyCode", keyCode);
        bodyJson.put("regionUUID", defaultRegion.uuid.toString());
      }
      return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
          defaultCustomer.createAuthToken(), bodyJson);
    }
  }

  private Result deleteAccessKey(UUID providerUUID, String keyCode) {
    String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" +
                providerUUID + "/access_keys/" + keyCode;
    return FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultCustomer.createAuthToken());
  }

  @Test
  public void testGetAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = getAccessKey(invalidProviderUUID, "foo");
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
  }

  @Test
  public void testGetAccessKeyWithInvalidKeyCode() {
    AccessKey accessKey = AccessKey.create(UUID.randomUUID(), "foo", new AccessKey.KeyInfo());
    Result result = getAccessKey(defaultProvider.uuid, accessKey.getKeyCode());
    assertEquals(BAD_REQUEST, result.status());
    assertBadRequest(result, "KeyCode not found: " + accessKey.getKeyCode());
  }

  @Test
  public void testGetAccessKeyWithValidKeyCode() {
    AccessKey accessKey = AccessKey.create(defaultProvider.uuid, "foo", new AccessKey.KeyInfo());
    Result result = getAccessKey(defaultProvider.uuid, accessKey.getKeyCode());
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    JsonNode idKey = json.get("idKey");
    assertValue(idKey, "keyCode", accessKey.getKeyCode());
    assertValue(idKey, "providerUUID", accessKey.getProviderUUID().toString());
  }

  @Test
  public void testListAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = listAccessKey(invalidProviderUUID);
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
  }

  @Test
  public void testListAccessKeyWithEmptyData() {
    Result result = listAccessKey(defaultProvider.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 0);
  }

  @Test
  public void testListAccessKeyWithValidData() {
    AccessKey.create(defaultProvider.uuid, "key-1", new AccessKey.KeyInfo());
    AccessKey.create(defaultProvider.uuid, "key-2", new AccessKey.KeyInfo());
    Result result = listAccessKey(defaultProvider.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json.size(), 2);
    json.forEach((key) -> {
      assertThat(key.get("idKey").get("keyCode").asText(), allOf(notNullValue(), containsString("key-")));
      assertThat(key.get("idKey").get("providerUUID").asText(), allOf(notNullValue(), equalTo(defaultProvider.uuid.toString())));
    });
  }

  @Test
  public void testCreateAccessKeyWithInvalidProviderUUID() {
    Result result = createAccessKey(UUID.randomUUID(), "foo", false);
    assertBadRequest(result, "Invalid Provider/Region UUID");
  }

  @Test
  public void testCreateAccessKeyWithInvalidParams() {
    Result result = createAccessKey(defaultProvider.uuid, null, false);
    JsonNode node = Json.parse(contentAsString(result));
    assertErrorNodeValue(node, "keyCode", "This field is required");
    assertErrorNodeValue(node, "regionUUID", "This field is required");
  }

  @Test
  public void testCreateAccessKeyWithDifferentProviderUUID() {
    Provider gceProvider = ModelFactory.gceProvider(ModelFactory.testCustomer("foo@bar.com"));
    Result result = createAccessKey(gceProvider.uuid, "key-code", false);
    assertBadRequest(result, "Invalid Provider/Region UUID");
  }

  @Test
  public void testCreateAccessKeyInDifferentRegion() {
    AccessKey accessKey = AccessKey.create(defaultProvider.uuid, "key-code", new AccessKey.KeyInfo());
    when(mockAccessManager.addKey(defaultRegion.uuid, "key-code")).thenReturn(accessKey);
    Result result = createAccessKey(defaultProvider.uuid, "key-code", false);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json.get("idKey"), "keyCode", "key-code");
    assertValue(json.get("idKey"), "providerUUID", defaultProvider.uuid.toString());
  }

  @Test
  public void testCreateAccessKeyWithoutKeyFile() {
    AccessKey accessKey = AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());
    when(mockAccessManager.addKey(defaultRegion.uuid, "key-code-1")).thenReturn(accessKey);
    Result result = createAccessKey(defaultProvider.uuid, "key-code-1", false);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json.get("idKey"), "keyCode", "key-code-1");
    assertValue(json.get("idKey"), "providerUUID", defaultProvider.uuid.toString());
  }

  @Test
  public void testCreateAccessKeyWithKeyFile() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    AccessKey accessKey = AccessKey.create(defaultProvider.uuid, "key-code-1", keyInfo);
    ArgumentCaptor<File> updatedFile = ArgumentCaptor.forClass(File.class);
    when(mockAccessManager.uploadKeyFile(eq(defaultRegion.uuid), any(File.class),
        eq("key-code-1"), eq(AccessManager.KeyType.PRIVATE))).thenReturn(accessKey);
    Result result = createAccessKey(defaultProvider.uuid, "key-code-1", true);
    Mockito.verify(mockAccessManager, times(1)).uploadKeyFile(eq(defaultRegion.uuid),
        updatedFile.capture(), eq("key-code-1"), eq(AccessManager.KeyType.PRIVATE));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertNotNull(json);
    try {
      List<String> lines = Files.readAllLines(updatedFile.getValue().toPath());
      assertEquals(1, lines.size());
      assertThat(lines.get(0), allOf(notNullValue(), equalTo("PRIVATE KEY DATA")));
    } catch (IOException e) {
      assertNull(e.getMessage());
    }
  }

  @Test
  public void testCreateAccessKeyWithException() {
    when(mockAccessManager.addKey(defaultRegion.uuid, "key-code-1"))
        .thenThrow(new RuntimeException("Something went wrong!!"));
    Result result = createAccessKey(defaultProvider.uuid, "key-code-1", false);
    assertErrorResponse(result, "Something went wrong!!");
  }

  @Test
  public void testDeleteAccessKeyWithInvalidProviderUUID() {
    UUID invalidProviderUUID = UUID.randomUUID();
    Result result = deleteAccessKey(invalidProviderUUID, "foo");
    assertBadRequest(result, "Invalid Provider UUID: " + invalidProviderUUID);
  }

  @Test
  public void testDeleteAccessKeyWithInvalidAccessKeyCode() {
    Result result = deleteAccessKey(defaultProvider.uuid, "foo");
    assertBadRequest(result, "KeyCode not found: foo");
  }

  @Test
  public void testDeleteAccessKeyWithValidAccessKeyCode() {
    AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());
    Result result = deleteAccessKey(defaultProvider.uuid, "key-code-1");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Deleted KeyCode: key-code-1")));
  }
}
