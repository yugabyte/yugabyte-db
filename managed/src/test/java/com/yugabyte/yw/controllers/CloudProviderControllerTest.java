// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static junit.framework.TestCase.assertTrue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public class CloudProviderControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderControllerTest.class);
  Customer customer;

  AccessManager mockAccessManager;
  NetworkManager mockNetworkManager;
  TemplateManager mockTemplateManager;

  @Override
  protected Application provideApplication() {
    ApiHelper mockApiHelper = mock(ApiHelper.class);
    mockAccessManager = mock(AccessManager.class);
    mockNetworkManager = mock(NetworkManager.class);
    mockTemplateManager = mock(TemplateManager.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(AccessManager.class).toInstance(mockAccessManager))
        .overrides(bind(NetworkManager.class).toInstance(mockNetworkManager))
        .overrides(bind(TemplateManager.class).toInstance(mockTemplateManager))
        .build();
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    new File(TestHelper.TMP_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TestHelper.TMP_PATH));
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
    Provider p2 = ModelFactory.gcpProvider(customer);
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
    Provider p = ModelFactory.gcpProvider(customer);
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
    List<String> providerCodes = ImmutableList.of("aws", "gcp");
    for (String code : providerCodes) {
      String providerName = code + "-Provider";
      ObjectNode bodyJson = Json.newObject();
      bodyJson.put("code", code);
      bodyJson.put("name", providerName);
      ObjectNode configJson = Json.newObject();
      ObjectNode configFileJson = Json.newObject();
      if (code.equals("gcp")) {
        // Technically this is not the input format of the file, but we're using this to match the
        // number of elements...
        configFileJson.put("GCE_EMAIL", "email");
        configFileJson.put("GCE_PROJECT", "project");
        configFileJson.put("GOOGLE_APPLICATION_CREDENTIALS", "credentials");
        configJson.put("config_file_contents", configFileJson);
      } else if (code.equals("aws")) {
        configJson.put("foo", "bar");
        configJson.put("foo2", "bar2");
      }
      bodyJson.set("config", configJson);
      Result result = createProvider(bodyJson);
      JsonNode json = Json.parse(contentAsString(result));
      assertOk(result);
      assertValue(json, "name", providerName);
      Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
      Map<String, String> config = provider.getConfig();
      assertFalse(config.isEmpty());
      // We should technically check the actual content, but the keys are different between the
      // input payload and the saved config.
      if (code.equals("gcp")) {
        assertEquals(configFileJson.size(), config.size());
      } else {
        assertEquals(configJson.size(), config.size());
      }
    }
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
    Universe universe = createUniverse(customer.getCustomerId());
    UniverseDefinitionTaskParams.UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = p.code;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    userIntent.regionList = new ArrayList<UUID>();
    userIntent.regionList.add(r.uuid);
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

  @Test
  public void testDeleteProviderWithProvisionScript() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    String scriptFile = createTempFile("provision_instance.py", "some script");
    keyInfo.provisionInstanceScript = scriptFile;
    AccessKey.create(p.uuid, "access-key-code", keyInfo);
    Result result = deleteProvider(p.uuid);
    assertOk(result);
    assertFalse(new File(scriptFile).exists());
  }
}
