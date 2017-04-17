// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.*;
import static junit.framework.TestCase.assertTrue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import java.util.List;
import java.util.Map;
import java.util.UUID;

public class CloudProviderControllerTest extends FakeDBApplication {
  Customer customer;

  AccessManager mockAccessManager;
  NetworkManager mockNetworkManager;

  @Override
  protected Application provideApplication() {
    ApiHelper mockApiHelper = mock(ApiHelper.class);
    mockAccessManager = mock(AccessManager.class);
    mockNetworkManager = mock(NetworkManager.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
        .overrides(bind(NetworkManager.class).toInstance(mockNetworkManager))
        .build();
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  private Result listProviders() {
    return FakeApiHelper.doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid  + "/providers", customer.createAuthToken());
  }

  private Result createProvider(JsonNode bodyJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST",
        "/api/customers/" + customer.uuid + "/providers", customer.createAuthToken(), bodyJson);
  }

  private Result deleteProvider(UUID providerUUID) {
    return FakeApiHelper.doRequestWithAuthToken("DELETE",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID, customer.createAuthToken());
  }

  @Test
  public void testListEmptyProviders() {
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertTrue(json.isArray());
    assertEquals(0, json.size());
  }

  @Test
  public void testListProviders() {
    Provider p1 = ModelFactory.awsProvider(customer);
    Provider p2 = ModelFactory.gceProvider(customer);
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertEquals(2, json.size());
    assertValues(json, "uuid", (List) ImmutableList.of(p1.uuid.toString(), p2.uuid.toString()));
    assertValues(json, "name", (List) ImmutableList.of(p1.name, p2.name));
  }
  @Test
  public void testListProvidersWithValidCustomer() {
    Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    Provider p = ModelFactory.gceProvider(customer);
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertEquals(1, json.size());
    assertValues(json, "uuid", (List) ImmutableList.of(p.uuid.toString()));
    assertValues(json, "name", (List) ImmutableList.of(p.name.toString()));
  }

  @Test
  public void testCreateProvider() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "azu");
    bodyJson.put("name", "Microsoft");
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Microsoft");
    assertValue(json, "customerUUID", customer.uuid.toString());
  }

  @Test
  public void testCreateDuplicateProvider() {
    ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    Result result = createProvider(bodyJson);
    assertBadRequest(result, "Duplicate provider code: aws");
  }

  @Test
  public void testCreateProviderWithDifferentCustomer() {
    Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Amazon");
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    Result result = createProvider(bodyJson);
    assertBadRequest(result, "\"name\":[\"This field is required\"]}");
  }

  @Test
  public void testCreateProviderWithConfig() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    ObjectNode configJson = Json.newObject();
    configJson.put("config-1", "Configuration 1");
    configJson.put("config-2", "Configuration 2");
    bodyJson.set("config", configJson);
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Amazon");
    Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = provider.getConfig();
    assertFalse(config.isEmpty());
    assertEquals(configJson, Json.toJson(config));
  }

  @Test
  public void testDeleteProviderWithAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "region 1", "yb image");
    AccessKey.create(p.uuid, "access-key-code", new AccessKey.KeyInfo());
    Result result = deleteProvider(p.uuid);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.asText(),
        allOf(notNullValue(), equalTo("Deleted provider: " + p.uuid)));
    assertEquals(0, AccessKey.getAll(p.uuid).size());
    assertNull(Provider.get(p.uuid));
  }

  @Test
  public void testDeleteProviderWithInvalidProviderUUID() {
    UUID providerUUID = UUID.randomUUID();
    Result result = deleteProvider(providerUUID);
    assertBadRequest(result, "Invalid Provider UUID: " + providerUUID);
  }

  @Test
  public void testDeleteProviderWithUniverses() {
    Provider p = ModelFactory.awsProvider(customer);
    Universe universe = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    UniverseDefinitionTaskParams.UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = p.code;
    universe = Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent));
    customer.addUniverseUUID(universe.universeUUID);
    customer.save();
    Result result = deleteProvider(p.uuid);
    assertBadRequest(result, "Cannot delete Provider with Universes");
  }

  @Test
  public void testDeleteProviderWithoutAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    Result result = deleteProvider(p.uuid);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.asText(),
        allOf(notNullValue(), equalTo("Deleted provider: " + p.uuid)));
    assertNull(Provider.get(p.uuid));
  }
}
