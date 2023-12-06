/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.AwsKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestControllerTest extends FakeDBApplication {
  private Customer customer;
  private Users user;
  private Universe universe;
  private String authToken;

  String mockEncryptionKey = "RjZiNzVGekljNFh5Zmh0NC9FQ1dpM0FaZTlMVGFTbW1Wa1dnaHRzdDhRVT0=";
  String algorithm = "AES";
  int keySize = 256;
  String mockKid = "some_kId";

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    universe = ModelFactory.createUniverse();
    authToken = user.createAuthToken();
    String mockApiKey = "some_api_key";
    Map<String, String> authorizationHeaders =
        ImmutableMap.of("Authorization", String.format("Basic %s", mockApiKey));
    ObjectNode createReqPayload =
        Json.newObject()
            .put("name", universe.getUniverseUUID().toString())
            .put("obj_type", algorithm)
            .put("key_size", keySize);
    ArrayNode keyOps = Json.newArray().add("EXPORT").add("APPMANAGEABLE");
    createReqPayload.set("key_ops", keyOps);
    Map<String, String> postReqHeaders =
        ImmutableMap.of(
            "Authorization",
            String.format("Bearer %s", mockApiKey),
            "Content-Type",
            "application/json");
    Map<String, String> getReqHeaders =
        ImmutableMap.of("Authorization", String.format("Bearer %s", mockApiKey));
    String getKeyUrl = String.format("https://some_base_url/crypto/v1/keys/%s/export", mockKid);
    Map<String, String> mockQueryParams =
        ImmutableMap.of("name", universe.getUniverseUUID().toString(), "limit", "1");
    // confGetter is not used in class SmartKeyEARService, so we can pass null.
    when(mockEARManager.getServiceInstance(eq("SMARTKEY")))
        .thenReturn(new SmartKeyEARService(null));
  }

  @Test
  public void testListKMSConfigs() {
    ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", Json.newObject());
    String url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs";
    Result listResult = doRequestWithAuthToken("GET", url, authToken);
    assertOk(listResult);
    JsonNode json = Json.parse(contentAsString(listResult));
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListEmptyConfigList() {
    String url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs";
    Result listResult = doRequestWithAuthToken("GET", url, authToken);
    assertOk(listResult);
    JsonNode json = Json.parse(contentAsString(listResult));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteConfig() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(KMSConfigTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", Json.newObject());
    String url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs";
    Result listResult = doRequestWithAuthToken("GET", url, authToken);
    assertOk(listResult);
    JsonNode json = Json.parse(contentAsString(listResult));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
    UUID kmsConfigUUID =
        UUID.fromString(((ArrayNode) json).get(0).get("metadata").get("configUUID").asText());
    url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs/" + kmsConfigUUID.toString();
    Result deleteResult = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(deleteResult);
    json = Json.parse(contentAsString(deleteResult));
    UUID taskUUID = UUID.fromString(json.get("taskUUID").asText());
    assertNotNull(taskUUID);
    assertAuditEntry(1, customer.getUuid());
  }

  @Ignore(
      "This test passes locally but fails on Jenkins due to Guice not injecting mocked ApiHelper"
          + " for an unknown reason")
  @Test
  public void testCreateAndRecoverKey() {
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/SMARTKEY";
    ObjectNode kmsConfigReq =
        Json.newObject().put("base_url", "some_base_url").put("api_key", "some_api_token");
    Result createKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertOk(createKMSResult);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/kms/SMARTKEY/create_key";
    ObjectNode createPayload =
        Json.newObject()
            .put("kms_provider", "SMARTKEY")
            .put("algorithm", algorithm)
            .put("key_size", Integer.toString(keySize));
    Result createKeyResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, createPayload));
    assertOk(createKeyResult);
    JsonNode json = Json.parse(contentAsString(createKeyResult));
    String keyValue = json.get("value").asText();
    assertEquals(keyValue, mockEncryptionKey);
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testCreateSMARTKEYKmsProviderWithInvalidAPIUrl() {
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/SMARTKEY";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("base_url", "some_base_url")
            .put("api_key", "some_api_token")
            .put("name", "test");
    Result createKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(createKMSResult, "Invalid API URL.");
  }

  @Test
  public void testCreateSMARTKEYKmsProviderWithValidAPIUrlButInvalidAPIKey() {
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/SMARTKEY";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("base_url", "api.amer.smartkey.io")
            .put("api_key", "some_api_token")
            .put("name", "test");
    Result createKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(createKMSResult, "Invalid API Key.");
  }

  @Test
  public void testCreateAwsKmsProviderWithInvalidCreds() {
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/AWS";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, "aws_accesscode")
            .put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-1")
            .put(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, "aws_secretKey")
            .put("name", "test");
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);
    Result createKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(createKMSResult, "Invalid AWS Credentials.");
  }

  @Test
  public void testCreateAwsKmsProviderWithValidCreds() {
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/AWS";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, "valid_accessKey")
            .put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-1")
            .put(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, "valid_secretKey")
            .put(AwsKmsAuthConfigField.ENDPOINT.fieldName, "https://kms.ap-south-1.amazonaws.com")
            .put("name", "test");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(KMSConfigTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);
    when(mockCloudAPI.isValidCredsKms(any(), any())).thenReturn(true);
    Result createKMSResult =
        doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq);
    assertOk(createKMSResult);
  }

  @Test
  public void testRecoverKeyNotFound() {
    UUID configUUID =
        ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", Json.newObject())
            .getConfigUUID();
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/kms";
    ObjectNode body =
        Json.newObject()
            .put("reference", "NzNiYmY5M2UtNWYyNy00NzE3LTgyYTktMTVjYzUzMDIzZWRm")
            .put("configUUID", configUUID.toString());
    Result recoverKeyResult =
        assertPlatformException(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, body));
    JsonNode json = Json.parse(contentAsString(recoverKeyResult));
    String expectedErrorMsg =
        String.format(
            "No universe key found for universe %s", universe.getUniverseUUID().toString());
    assertErrorNodeValue(json, expectedErrorMsg);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateKMSProviderWithDuplicateName() {
    ObjectNode authConfig = Json.newObject().put(AwsKmsAuthConfigField.CMK_ID.fieldName, "test_id");
    ModelFactory.createKMSConfig(customer.getUuid(), "AWS", authConfig, "test");
    String kmsConfigUrl = "/api/customers/" + customer.getUuid() + "/kms_configs/AWS";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, "valid_accessKey")
            .put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-1")
            .put(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, "valid_secretKey")
            .put(AwsKmsAuthConfigField.ENDPOINT.fieldName, "https://kms.ap-south-1.amazonaws.com")
            .put("name", "test");
    Result createKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(createKMSResult, "Kms config with test name already exists");
  }

  @Test
  public void testEditKMSConfigWithInvalidConfigUUID() {
    UUID invalidConfigUUID = UUID.randomUUID();
    String kmsConfigUrl =
        "/api/customers/" + customer.getUuid() + "/kms_configs/" + invalidConfigUUID + "/edit";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, "valid_accessKey")
            .put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-1")
            .put(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, "valid_secretKey")
            .put(AwsKmsAuthConfigField.ENDPOINT.fieldName, "https://kms.ap-south-1.amazonaws.com")
            .put("name", "test");
    Result updateKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    String errMsg =
        "KMS config with config UUID "
            + invalidConfigUUID
            + " does not exist for customer "
            + customer.getUuid();
    assertBadRequest(updateKMSResult, errMsg);
  }

  @Test
  public void testEditKMSConfigWithValidParams() {

    ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", Json.newObject());
    String url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs";
    Result listResult = doRequestWithAuthToken("GET", url, authToken);
    assertOk(listResult);
    JsonNode json = Json.parse(contentAsString(listResult));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
    UUID kmsConfigUUID =
        UUID.fromString(((ArrayNode) json).get(0).get("metadata").get("configUUID").asText());
    String kmsConfigUrl =
        "/api/v1/customers/" + customer.getUuid() + "/kms_configs/" + kmsConfigUUID + "/edit";
    ObjectNode kmsConfigReq = Json.newObject().put("base_url", "api.amer.smartkey.io");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(KMSConfigTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Result updateKMSResult =
        doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq);
    assertOk(updateKMSResult);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testEditKMSConfigName() {
    ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", Json.newObject());
    String url = "/api/v1/customers/" + customer.getUuid() + "/kms_configs";
    Result listResult = doRequestWithAuthToken("GET", url, authToken);
    assertOk(listResult);
    JsonNode json = Json.parse(contentAsString(listResult));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
    UUID kmsConfigUUID =
        UUID.fromString(((ArrayNode) json).get(0).get("metadata").get("configUUID").asText());
    String kmsConfigUrl =
        "/api/v1/customers/" + customer.getUuid() + "/kms_configs/" + kmsConfigUUID + "/edit";
    ObjectNode kmsConfigReq =
        Json.newObject().put("base_url", "api.amer.smartkey.io").put("name", "test");

    Result updateKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(updateKMSResult, "KmsConfig name cannot be changed");
  }

  @Test
  public void testEditAWSKMSConfigRegion() {
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put(AwsKmsAuthConfigField.ACCESS_KEY_ID.fieldName, "valid_accessKey")
            .put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-1")
            .put(AwsKmsAuthConfigField.SECRET_ACCESS_KEY.fieldName, "valid_secretKey")
            .put(AwsKmsAuthConfigField.ENDPOINT.fieldName, "https://kms.ap-south-1.amazonaws.com")
            .put("name", "test")
            .put(AwsKmsAuthConfigField.CMK_ID.fieldName, "3ccb9374-bc6e-4247-9cc5-2170ec77053e");
    ModelFactory.createKMSConfig(customer.getUuid(), "AWS", kmsConfigReq, "test");
    KmsConfig config = KmsConfig.listKMSConfigs(customer.getUuid()).get(0);
    String kmsConfigUrl =
        "/api/customers/" + customer.getUuid() + "/kms_configs/" + config.getConfigUUID() + "/edit";
    kmsConfigReq.put(AwsKmsAuthConfigField.REGION.fieldName, "ap-south-2");
    Result updateKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(updateKMSResult, "AWS KmsConfig field 'AWS_REGION' cannot be changed.");
  }

  @Test
  public void testEditAZUKMSConfigKeyName() {
    // Negative test case where a field that should not be changed is attempted to be edited
    // Create the request body with all test fields
    KeyProvider keyProvider = KeyProvider.valueOf("AZU");
    String configName = "test-azu-kms-config";
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("name", configName)
            .put(AzuKmsAuthConfigField.CLIENT_ID.fieldName, "test-client-id")
            .put(AzuKmsAuthConfigField.CLIENT_SECRET.fieldName, "test-client-secret")
            .put(AzuKmsAuthConfigField.TENANT_ID.fieldName, "test-tenant-id")
            .put(AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName, "test-vault-url")
            .put(AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName, "test-key-name")
            .put(AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName, "RSA")
            .put(AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName, 2048);

    KmsConfig result =
        KmsConfig.createKMSConfig(customer.getUuid(), keyProvider, kmsConfigReq, configName);

    // Edit the key name field in the request body
    String kmsConfigUrl =
        String.format(
            "/api/customers/%s/kms_configs/%s/edit", customer.getUuid(), result.getConfigUUID());
    kmsConfigReq.put(AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName, "test-key-name-2");

    // Call the API and assert that the POST request throws exception
    Result updateKMSResult =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", kmsConfigUrl, authToken, kmsConfigReq));
    assertBadRequest(updateKMSResult, "AZU Kms config field 'AZU_KEY_NAME' cannot be changed.");
  }
}
