// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;

import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;

import org.mockito.runners.MockitoJUnitRunner;

import java.time.Clock;
import java.time.Instant;
import java.util.Map;


import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import com.google.common.collect.ImmutableMap;

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

  static final Logger LOG = LoggerFactory.getLogger(CallHomeManagerTest.class);

  private JsonNode callHomePayload(Customer c, Provider p, Universe u){
    ObjectNode expectedPayload = Json.newObject();
    expectedPayload.put("customer_uuid", c.uuid.toString());
    expectedPayload.put("code", c.code);
    expectedPayload.put("email", c.email);
    expectedPayload.put("creation_date", c.creationDate.toString());
    ArrayNode universes = Json.newArray();
    universes.add(u.toJson());
    ArrayNode providers = Json.newArray();
    ObjectNode provider = Json.newObject();
    provider.put("provider_uuid", p.uuid.toString());
    provider.put("code", p.code);
    provider.put("name", p.name);
    ArrayNode regions = Json.newArray();
    for (Region r : p.regions) {
      regions.add(r.details);
    }
    provider.put("regions", regions);
    providers.add(provider);
    expectedPayload.put("universes", universes);
    expectedPayload.put("providers", providers);
    Map<String, Object> ywMetadata = configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    expectedPayload.put("yugaware_uuid", ywMetadata.get("yugaware_uuid").toString());
    expectedPayload.put("version", ywMetadata.get("version").toString());
    expectedPayload.put("timestamp", clock.instant().getEpochSecond());
    return expectedPayload;
  }

  @Test
  public void testNoSendDiagnostics() {
    Customer mockCust = mock(Customer.class);
    when(mockCust.getCallHomeLevel()).thenReturn("NONE");
    // Need to save customer with the new universe or else Customer.getUniverses() won't find any.
    callHomeManager.sendDiagnostics(mockCust);
    verifyZeroInteractions(apiHelper);
  }

  @Test
  public void testCollectDiagnostics() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
      .thenReturn(ImmutableMap.of("yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f",
                                  "version", "0.0.1"));
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    Customer c = ModelFactory.testCustomer();
    Provider p = ModelFactory.awsProvider(c);
    Universe u = ModelFactory.createUniverse(c.getCustomerId());
    c.addUniverseUUID(u.universeUUID);
    // Need to save customer with the new universe or else Customer.getUniverses() won't find any.
    c.save();
    JsonNode expectedPayload = callHomePayload(c, p, u);
    JsonNode actualPayload = callHomeManager.CollectDiagnostics(c);
    Assert.assertEquals(expectedPayload, actualPayload);
  }
}
