// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;

import org.asynchttpclient.util.Base64;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;

import org.mockito.runners.MockitoJUnitRunner;

import java.time.Clock;
import java.time.Instant;
import java.util.Map;
import java.util.UUID;

import play.libs.Json;

import com.google.common.collect.ImmutableMap;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class CallHomeManagerTest extends FakeDBApplication {

  @InjectMocks
  CallHomeManager callHomeManager;

  @Mock
  ConfigHelper configHelper;

  @Mock
  ApiHelper apiHelper;

  @Mock
  Clock clock;

  Customer defaultCustomer;
  Users defaultUser;
  Provider defaultProvider;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
  }

  private JsonNode callHomePayload(Universe universe){
    ObjectNode expectedPayload = Json.newObject();
    expectedPayload.put("customer_uuid", defaultCustomer.uuid.toString());
    expectedPayload.put("code", defaultCustomer.code);
    expectedPayload.put("email", defaultUser.email);
    expectedPayload.put("creation_date", defaultCustomer.creationDate.toString());
    ArrayNode universes = Json.newArray();
    if (universe != null) {
      universes.add(universe.toJson());
    }
    ArrayNode providers = Json.newArray();
    ObjectNode provider = Json.newObject();
    provider.put("provider_uuid", defaultProvider.uuid.toString());
    provider.put("code", defaultProvider.code);
    provider.put("name", defaultProvider.name);
    ArrayNode regions = Json.newArray();
    for (Region r : defaultProvider.regions) {
      regions.add(r.details);
    }
    provider.set("regions", regions);
    providers.add(provider);
    expectedPayload.set("universes", universes);
    expectedPayload.set("providers", providers);
    Map<String, Object> ywMetadata = configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    expectedPayload.put("yugaware_uuid", ywMetadata.get("yugaware_uuid").toString());
    expectedPayload.put("version", ywMetadata.get("version").toString());
    expectedPayload.put("timestamp", clock.instant().getEpochSecond());
    expectedPayload.set("errors", Json.newArray());
    return expectedPayload;
  }

  @Test
  public void testSendDiagnostics() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(ImmutableMap.of("yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f",
            "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    when(apiHelper.postRequest(anyString(), any(), anyMap())).thenReturn(Json.toJson(responseJson));
    callHomeManager.sendDiagnostics(defaultCustomer);

    ArgumentCaptor<String> url = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<JsonNode> params = ArgumentCaptor.forClass(JsonNode.class);
    ArgumentCaptor<Map> headers = ArgumentCaptor.forClass(Map.class);

    verify(apiHelper).postRequest(url.capture(), params.capture(), (Map<String, String>) headers.capture());
    ObjectNode expectedPayload = (ObjectNode) callHomePayload(null);
    assertEquals(expectedPayload, params.getValue());

    System.out.println(params.getValue());
    String expectedToken = Base64.encode(defaultCustomer.uuid.toString().getBytes());
    assertEquals(expectedToken, headers.getValue().get("X-AUTH-TOKEN"));
    assertEquals("http://yw-diagnostics.yugabyte.com", url.getValue());
  }

  @Test
  public void testCollectDiagnostics() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
      .thenReturn(ImmutableMap.of("yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f",
                                  "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    defaultCustomer.addUniverseUUID(u.universeUUID);
    // Need to save customer with the new universe or else Customer.getUniverses() won't find any.
    defaultCustomer.save();
    JsonNode expectedPayload = callHomePayload(u);
    JsonNode actualPayload = callHomeManager.CollectDiagnostics(defaultCustomer, CollectionLevel.MEDIUM);
    assertEquals(expectedPayload, actualPayload);
  }

  @Test
  public void testCollectDiagnosticsWithInvalidUniverses() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(ImmutableMap.of("yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f",
            "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    Universe u1 = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    UUID unknownUniverse = UUID.randomUUID();
    defaultCustomer.addUniverseUUID(u1.universeUUID);
    defaultCustomer.addUniverseUUID(unknownUniverse);

    // Need to save customer with the new universe or else Customer.getUniverses() won't find any.
    defaultCustomer.save();
    ObjectNode expectedPayload = (ObjectNode) callHomePayload(u1);
    expectedPayload.set("errors", Json.newArray().add("Cannot find universe " + unknownUniverse));
    JsonNode actualPayload = callHomeManager.CollectDiagnostics(defaultCustomer, CollectionLevel.MEDIUM);
    assertEquals(expectedPayload.get("errors"), actualPayload.get("errors"));
  }

  @Test
  public void testNoSendDiagnostics() {
    ModelFactory.setCallhomeLevel(defaultCustomer, "NONE");
    callHomeManager.sendDiagnostics(defaultCustomer);
    verifyZeroInteractions(apiHelper);
  }
}
