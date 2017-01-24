// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class AccessKeyControllerTest extends FakeDBApplication {
    Provider defaultProvider;
    Customer defaultCustomer;
    @Before
    public void setUp() {
        defaultCustomer = Customer.create("Test Customer", "foo@bar.com", "password");
        defaultProvider = Provider.create("provider-1", "Sample Provider");
    }

    @Test
    public void testGetAccessKeyWithInvalidProviderUUID() {
        UUID invalidProviderUUID = UUID.randomUUID();
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + invalidProviderUUID + "/access_keys/" + "foo";
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Provider UUID: " + invalidProviderUUID)));
    }

    @Test
    public void testGetAccessKeyWithInvalidKeyCode() {
        AccessKey accessKey = AccessKey.create(UUID.randomUUID(), "foo", new AccessKey.KeyInfo());

        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys/" + accessKey.getKeyCode();
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("KeyCode not found: " + accessKey.getKeyCode())));
    }

    @Test
    public void testGetAccessKeyWithValidKeyCode() {
        AccessKey accessKey = AccessKey.create(defaultProvider.uuid, "foo", new AccessKey.KeyInfo());
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys/" + accessKey.getKeyCode();
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        JsonNode idKey = json.get("idKey");
        assertThat(idKey, is(notNullValue()));
        assertThat(idKey.get("keyCode").asText(), allOf(notNullValue(), equalTo(accessKey.getKeyCode())));
        assertThat(idKey.get("providerUUID").asText(), allOf(notNullValue(), equalTo(accessKey.getProviderUUID().toString())));
    }

    @Test
    public void testListAccessKeyWithInvalidProviderUUID() {
        UUID invalidProviderUUID = UUID.randomUUID();
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" + invalidProviderUUID + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Provider UUID: " + invalidProviderUUID)));
    }

    @Test
    public void testListAccessKeyWithEmptyData() {
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertTrue(json.isArray());
        assertEquals(json.size(), 0);
    }

    @Test
    public void testListAccessKeyWithValidData() {
        AccessKey.create(defaultProvider.uuid, "key-1", new AccessKey.KeyInfo());
        AccessKey.create(defaultProvider.uuid, "key-2", new AccessKey.KeyInfo());
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthToken("GET", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertTrue(json.isArray());
        assertEquals(json.size(), 2);
        json.forEach((key) -> {
            assertThat(key.get("idKey").get("keyCode").asText(), allOf(notNullValue(), containsString("key-")));
            assertThat(key.get("idKey").get("providerUUID").asText(), allOf(notNullValue(), equalTo(defaultProvider.uuid.toString())));
        });
    }

    @Test
    public void testCreateAccessKeyWithInvalidProviderUUID() {
        UUID invalidProviderUUID = UUID.randomUUID();
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" + invalidProviderUUID + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthToken("POST", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Provider UUID: " + invalidProviderUUID)));
    }

    @Test
    public void testCreateAccessKeyWithInvalidParams() {
        JsonNode bodyJson = Json.newObject();
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("\"keyCode\":[\"This field is required\"]")));
        assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("\"keyInfo\":[\"This field is required\"]")));
    }

    @Test
    public void testCreateAccessKeyWithIncorrectKeyInfoParam() {
        ObjectNode bodyJson = Json.newObject();
        bodyJson.put("keyCode", "key-code-1");
        bodyJson.put("keyInfo", "something");

        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));
        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").toString(), allOf(notNullValue(),
                containsString("\"keyInfo\":[\"Invalid value\"]")));
    }

    @Test
    public void testCreateAccessKeyWithValidParams() {
        ObjectNode bodyJson = Json.newObject();
        bodyJson.put("keyCode", "key-code-1");
        ObjectNode keyInfoJson = Json.newObject();
        keyInfoJson.put("publicKey", "public key..");
        bodyJson.set("keyInfo", keyInfoJson);
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertNotNull(json);
    }

    @Test
    public void testCreateAccessKeyWithDuplicateKeyCode() {
        AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());
        ObjectNode bodyJson = Json.newObject();
        bodyJson.put("keyCode", "key-code-1");
        ObjectNode keyInfoJson = Json.newObject();
        keyInfoJson.put("publicKey", "public key..");
        bodyJson.set("keyInfo", keyInfoJson);
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys";
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Duplicate keycode: key-code-1")));
    }

    @Test
    public void testDeleteAccessKeyWithInvalidProviderUUID() {
        UUID invalidProviderUUID = UUID.randomUUID();

        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" +
                invalidProviderUUID + "/access_keys/" + "foo";
        Result result = FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Provider UUID: " + invalidProviderUUID)));
    }

    @Test
    public void testDeleteAccessKeyWithInvalidAccessKeyCode() {
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" +
                defaultProvider.uuid + "/access_keys/" + "foo";
        Result result = FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("KeyCode not found: foo" )));
    }

    @Test
    public void testDeleteAccessKeyWithValidAccessKeyCode() {
        AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());

        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" +
                defaultProvider.uuid + "/access_keys/" + "key-code-1";
        Result result = FakeApiHelper.doRequestWithAuthToken("DELETE", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(OK, result.status());
        assertThat(json.asText(), allOf(notNullValue(), equalTo("Deleted KeyCode: key-code-1")));
    }

    @Test
    public void testUpdateAccessKeyWithInvalidProviderUUID() {
        UUID invalidProviderUUID = UUID.randomUUID();
        String uri = "/api/customers/" + defaultCustomer.uuid + "/providers/" + invalidProviderUUID + "/access_keys/" + "foo";
        Result result = FakeApiHelper.doRequestWithAuthToken("PUT", uri, defaultCustomer.createAuthToken());
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Provider UUID: " + invalidProviderUUID)));
    }

    @Test
    public void testUpdateAccessKeyWithInvalidParams() {
        JsonNode bodyJson = Json.newObject();
        AccessKey ak = AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());

        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys/" + ak.getKeyCode();
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("PUT", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("\"keyCode\":[\"This field is required\"]")));
        assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("\"keyInfo\":[\"This field is required\"]")));
    }

    @Test
    public void testUpdateAccessKeyWithInvalidKeyCode() {
        ObjectNode bodyJson = Json.newObject();
        bodyJson.put("keyCode", "key-code-1");
        ObjectNode keyInfoJson = Json.newObject();
        keyInfoJson.put("publicKey", "public key..");
        bodyJson.set("keyInfo", keyInfoJson);
        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys/" + "foo";
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("PUT", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));

        assertEquals(BAD_REQUEST, result.status());
        assertThat(json.get("error").toString(), allOf(notNullValue(), containsString("KeyCode not found: foo")));
    }

    @Test
    public void testUpdateAccessKeyWithValidParams() {
        AccessKey ak = AccessKey.create(defaultProvider.uuid, "key-code-1", new AccessKey.KeyInfo());

        ObjectNode bodyJson = Json.newObject();
        bodyJson.put("keyCode", "key-code-1");
        ObjectNode keyInfoJson = Json.newObject();
        keyInfoJson.put("publicKey", "public key..");
        keyInfoJson.put("privateKey", "private key..");
        bodyJson.set("keyInfo", keyInfoJson);

        String uri = "/api/customers/" + defaultCustomer.uuid +
                "/providers/" + defaultProvider.uuid + "/access_keys/" + ak.getKeyCode();
        Result result = FakeApiHelper.doRequestWithAuthTokenAndBody("PUT", uri,
                defaultCustomer.createAuthToken(), bodyJson);
        JsonNode json = Json.parse(contentAsString(result));
        assertEquals(OK, result.status());
        assertThat(json.asText(), allOf(notNullValue(), equalTo("Updated KeyCode: key-code-1")));
        ak = AccessKey.get(defaultProvider.uuid, "key-code-1");
        assertThat(ak.getKeyInfo().publicKey, allOf(notNullValue(),
                equalTo("public key..")));
        assertThat(ak.getKeyInfo().privateKey, allOf(notNullValue(),
                equalTo("private key..")));
    }
}
