// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CallHomeManagerTest extends FakeDBApplication {

  @InjectMocks CallHomeManager callHomeManager;

  @Mock ConfigHelper configHelper;

  @Mock ApiHelper apiHelper;

  @Mock Clock clock;

  Customer defaultCustomer;
  Users defaultUser;
  Provider defaultProvider;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
  }

  // Will fix after expectedUniverseVersion change is reviewed
  private JsonNode callHomePayload(Universe universe) {
    ObjectNode expectedPayload = Json.newObject();
    expectedPayload.put("customer_uuid", defaultCustomer.getUuid().toString());
    expectedPayload.put("code", defaultCustomer.getCode());
    expectedPayload.put("email", defaultUser.getEmail());
    expectedPayload.put("creation_date", defaultCustomer.getCreationDate().toString());
    List<UniverseResp> universes = new ArrayList<>();
    if (universe != null) {
      universes.add(new UniverseResp(universe));
    }
    ArrayNode providers = Json.newArray();
    ObjectNode provider = Json.newObject();
    provider.put("provider_uuid", defaultProvider.getUuid().toString());
    provider.put("code", defaultProvider.getCode());
    provider.put("name", defaultProvider.getName());
    ArrayNode regions = Json.newArray();
    for (Region r : defaultProvider.getRegions()) {
      regions.add(Json.toJson(r.getDetails()));
    }
    provider.set("regions", regions);
    providers.add(provider);
    expectedPayload.set("universes", Json.toJson(universes));
    expectedPayload.set("providers", providers);
    Map<String, Object> ywMetadata =
        configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    expectedPayload.put("yugaware_uuid", ywMetadata.get("yugaware_uuid").toString());
    expectedPayload.put("version", ywMetadata.get("version").toString());
    expectedPayload.put("timestamp", clock.instant().getEpochSecond());
    expectedPayload.set("errors", Json.newArray());
    return expectedPayload;
  }

  @Test
  public void testSendDiagnostics() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of(
                "yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f", "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    when(apiHelper.postRequest(anyString(), any(), anyMap())).thenReturn(Json.toJson(responseJson));
    callHomeManager.sendDiagnostics(defaultCustomer);

    ArgumentCaptor<String> url = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<JsonNode> params = ArgumentCaptor.forClass(JsonNode.class);
    ArgumentCaptor<Map<String, String>> headers = ArgumentCaptor.forClass(Map.class);

    verify(apiHelper).postRequest(url.capture(), params.capture(), headers.capture());
    ObjectNode expectedPayload = (ObjectNode) callHomePayload(null);
    assertEquals(expectedPayload, params.getValue());

    System.out.println(params.getValue());
    String expectedToken =
        Base64.getEncoder().encodeToString(defaultCustomer.getUuid().toString().getBytes());
    assertEquals(expectedToken, headers.getValue().get("X-AUTH-TOKEN"));
    assertEquals("https://yw-diagnostics.yugabyte.com", url.getValue());
  }

  @Test
  public void testCollectDiagnostics() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of(
                "yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f",
                "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    u.getUniverseDetails().expectedUniverseVersion = 1;
    u.update();
    u = Universe.getOrBadRequest(u.getUniverseUUID());

    JsonNode expectedPayload = callHomePayload(u);
    JsonNode actualPayload =
        callHomeManager.CollectDiagnostics(defaultCustomer, CollectionLevel.MEDIUM);
    assertEquals(expectedPayload, actualPayload);
  }

  @Test
  public void testNoSendDiagnostics() {
    ModelFactory.setCallhomeLevel(defaultCustomer, "NONE");
    callHomeManager.sendDiagnostics(defaultCustomer);
    verifyNoInteractions(apiHelper);
  }
}
