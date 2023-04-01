/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertNotFound;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class HAControllerTest extends FakeDBApplication {
  Customer customer;
  Users user;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  private Result createClusterKey() {
    String authToken = user.createAuthToken();
    return doRequestWithAuthToken("GET", "/api/settings/ha/generate_key", authToken);
  }

  @Test
  public void testCreateClusterKey() {
    assertOk(createClusterKey());
  }

  @Test
  public void testCreateHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertOk(createResult);
    JsonNode result = Json.parse(contentAsString(createResult));
    assertEquals(result.get("cluster_key").asText(""), clusterKey);
  }

  @Test
  public void testCreateMultipleHAConfigNotAllowed() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertOk(createResult);

    clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult2 = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertBadRequest(createResult2, "An HA Config already exists");
  }

  @Test
  public void testCreateAndGetHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertOk(createResult);

    JsonNode createResponse = Json.parse(contentAsString(createResult));
    UUID createConfigUUID = UUID.fromString(createResponse.get("uuid").asText());

    Result getResult = doRequestWithAuthToken("GET", uri, authToken);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    UUID getConfigUUID = UUID.fromString(getResponse.get("uuid").asText());

    assertEquals(createConfigUUID, getConfigUUID);
    assertEquals(getResponse.get("cluster_key").asText(""), clusterKey);
  }

  @Test
  public void testCreateAndEditHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertOk(createResult);

    Result getResult = doRequestWithAuthToken("GET", uri, authToken);
    assertOk(getResult);
    JsonNode result = Json.parse(contentAsString(getResult));
    UUID configUUID = UUID.fromString(result.get("uuid").asText());

    uri = uri + "/" + configUUID.toString();
    String editClusterKey =
        Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    body = Json.newObject().put("cluster_key", editClusterKey);
    Result editResult = doRequestWithAuthTokenAndBody("PUT", uri, authToken, body);

    assertOk(editResult);
    result = Json.parse(contentAsString(editResult));
    String editClusterKeyResponse = result.get("cluster_key").asText("");

    assertNotEquals(editClusterKeyResponse, clusterKey);
    assertEquals(editClusterKeyResponse, editClusterKey);
  }

  @Test
  public void testDeleteHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = Json.parse(contentAsString(createClusterKey())).get("cluster_key").asText();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);

    assertOk(createResult);

    JsonNode createResponse = Json.parse(contentAsString(createResult));
    UUID configUUID = UUID.fromString(createResponse.get("uuid").asText());

    String deleteURI = uri + "/" + configUUID.toString();

    Result deleteResult = doRequestWithAuthToken("DELETE", deleteURI, authToken);

    assertOk(deleteResult);

    Result getResult = doRequestWithAuthToken("GET", uri, authToken);
    assertNotFound(getResult, "No HA config exists");
  }
}
