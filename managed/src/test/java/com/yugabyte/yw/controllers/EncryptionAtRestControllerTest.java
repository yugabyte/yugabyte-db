// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import java.util.*;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;

import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.Application;
import play.Configuration;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestControllerTest extends WithApplication {
    private static Commissioner mockCommissioner;
    private static MetricQueryHelper mockMetricQueryHelper;

    @Mock
    play.Configuration mockAppConfig;
    private Customer customer;
    private String authToken;
    private ApiHelper mockApiHelper;
    private CallHome mockCallHome;
    private HealthChecker mockHealthChecker;
    private UUID mockUniverseUUID;

    String mockEncryptionKey = "RjZiNzVGekljNFh5Zmh0NC9FQ1dpM0FaZTlMVGFTbW1Wa1dnaHRzdDhRVT0=";
    String algorithm = "AES";
    int keySize = 256;
    String mockKid = "some_kId";


    @Override
    protected Application provideApplication() {
        mockCommissioner = mock(Commissioner.class);
        mockMetricQueryHelper = mock(MetricQueryHelper.class);
        mockApiHelper = mock(ApiHelper.class);
        mockCallHome = mock(CallHome.class);
        mockHealthChecker = mock(HealthChecker.class);
        mockUniverseUUID = UUID.randomUUID();
        return new GuiceApplicationBuilder()
                .configure((Map) Helpers.inMemoryDatabase())
                .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
                .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
                .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
                .overrides(bind(CallHome.class).toInstance(mockCallHome))
                .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
                .build();
    }

    @Before
    public void setUp() {
        customer = ModelFactory.testCustomer();
        authToken = customer.createAuthToken();
        String mockApiKey = "some_api_key";
        Map<String, String> authorizationHeaders = ImmutableMap.of(
                "Authorization", String.format("Basic %s", mockApiKey)
        );
        ObjectNode createReqPayload = Json.newObject()
                .put("name", mockUniverseUUID.toString())
                .put("obj_type", algorithm)
                .put("key_size", keySize);
        ArrayNode keyOps = Json.newArray()
                .add("EXPORT")
                .add("APPMANAGEABLE");
        createReqPayload.set("key_ops", keyOps);
        Map<String, String> postReqHeaders = ImmutableMap.of(
                "Authorization", String.format("Bearer %s", mockApiKey),
                "Content-Type", "application/json"
        );
        when(mockApiHelper.postRequest(any(String.class), any(ObjectNode.class), any(Map.class)))
                .thenReturn(Json.newObject().put("kid", mockKid));
        when(
                mockApiHelper.postRequest(
                        "https://some_base_url/sys/v1/session/auth",
                        null,
                        authorizationHeaders
                )
        ).thenReturn(Json.newObject().put("access_token", "some_access_token"));
        Map<String, String> getReqHeaders = ImmutableMap.of(
                "Authorization", String.format("Bearer %s", mockApiKey)
        );
        String getKeyUrl = String.format("https://some_base_url/crypto/v1/keys/%s/export", mockKid);
        when(mockApiHelper.getRequest(any(String.class), any(Map.class)))
                .thenReturn(Json.newObject().put("value", mockEncryptionKey));
        Map<String, String> mockQueryParams = ImmutableMap.of(
                "name", mockUniverseUUID.toString(),
                "limit", "1"
        );
        when(mockApiHelper.getRequest(any(String.class), any(Map.class), any(Map.class)))
                .thenReturn(Json.newArray());
    }

    @Test
    public void testCreateAndListKMSConfigs() {
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs/SMARTKEY";
        Result createResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                Json.newObject()
        );
        assertOk(createResult);
        url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 1);
    }

    @Test
    public void testListEmptyConfigList() {
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 0);
    }

    @Test
    public void testDeleteConfig() {
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs/SMARTKEY";
        Result createResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                Json.newObject()
        );
        assertOk(createResult);
        Result deleteResult = doRequestWithAuthToken("DELETE", url, authToken);
        assertOk(deleteResult);
        url = "/api/v1/customers/" + customer.uuid + "/kms_configs";
        Result listResult = doRequestWithAuthToken("GET", url, authToken);
        assertOk(listResult);
        JsonNode json = Json.parse(contentAsString(listResult));
        assertTrue(json.isArray());
        assertEquals(json.size(), 0);
    }

    @Test
    public void testCreateAndRecoverKey() {
        String mockApiKey = "some_api_key";
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs/SMARTKEY";
        ObjectNode configPayload = Json.newObject()
                .put("base_url", "some_base_url")
                .put("api_key", mockApiKey);
        Result createConfigResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                configPayload
        );
        assertOk(createConfigResult);
        url = "/api/v1/customers/" + customer.uuid + "/universes/" + mockUniverseUUID +
                "/kms/SMARTKEY/create_key";
        ObjectNode createPayload = Json.newObject()
                .put("kms_provider", "SMARTKEY")
                .put("algorithm", algorithm)
                .put("key_size", Integer.toString(keySize));
        Result createKeyResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                createPayload
        );
        assertOk(createKeyResult);
        JsonNode json = Json.parse(contentAsString(createKeyResult));
        String keyValue = json.get("value").asText();
        assertEquals(keyValue, mockEncryptionKey);
    }

    @Test
    public void testRecoverKeyNotFound() {
        when(mockApiHelper.getRequest(any(String.class), any(Map.class), any(Map.class)))
                .thenReturn(Json.newArray().add(Json.newObject().put("kid", mockKid)));
        String mockApiKey = "some_api_key";
        String url = "/api/v1/customers/" + customer.uuid + "/kms_configs/SMARTKEY";
        ObjectNode configPayload = Json.newObject()
                .put("base_url", "some_base_url")
                .put("api_key", mockApiKey);
        Result createConfigResult = doRequestWithAuthTokenAndBody(
                "POST",
                url,
                authToken,
                configPayload
        );
        assertOk(createConfigResult);
        url = "/api/v1/customers/" + customer.uuid + "/universes/" + mockUniverseUUID +
                "/kms/SMARTKEY/recover_key";
        Result recoverKeyResult = doRequestWithAuthToken("GET", url, authToken);
        JsonNode json = Json.parse(contentAsString(recoverKeyResult));
        assertErrorNodeValue(json, String.format(
                "No key found for customer %s for universe %s with provider SMARTKEY",
                customer.uuid.toString(),
                mockUniverseUUID.toString()
        ));
    }
}
