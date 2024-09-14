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
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.time.Clock;
import java.time.Instant;
import java.util.Base64;
import java.util.Map;
import java.util.UUID;
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

  @Mock RuntimeConfGetter mockRuntimeConf;

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

  private void verifyCallHome(JsonNode result, Universe u) {
    Configuration config =
        Configuration.builder()
            .jsonProvider(new JacksonJsonNodeJsonProvider())
            .mappingProvider(new JacksonMappingProvider())
            .build();
    DocumentContext ctx = JsonPath.parse(result.deepCopy(), config);

    // verify a few fields at the top level
    assertEquals(defaultCustomer.getUuid(), ctx.read("$.customer_uuid", UUID.class));
    assertEquals(defaultCustomer.getCode(), ctx.read("$.code", String.class));
    Map<String, Object> ywMetadata =
        configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    assertEquals(ctx.read("$.version", String.class), ywMetadata.get("version").toString());
    assertEquals(new Boolean(false), ctx.read("$.k8s_operator.enabled", Boolean.class));
    assertEquals(new Boolean(false), ctx.read("$.k8s_operator.oss_community_mode", Boolean.class));

    if (u != null) {
      // verify a few univ fields
      assertEquals(new Long(1), ctx.read("$.universes.length()", Long.class));
      UniverseResp resp = new UniverseResp(u);
      assertEquals(ctx.read("$.universes[0].name", String.class), u.getName());
      assertEquals(
          ctx.read(
              "$.universes[0].universeDetails.clusters[0].userIntent.ybSoftwareVersion",
              String.class),
          resp.universeDetails.delegate.clusters.get(0).userIntent.ybSoftwareVersion);
    } else {
      assertEquals(new Long(0), ctx.read("$.universes.length()", Long.class));
    }

    // verify a few fields at the provider level
    assertEquals(new Long(1), ctx.read("$.providers.length()", Long.class));
    assertEquals(defaultProvider.getUuid(), ctx.read("$.providers[0].provider_uuid", UUID.class));
    assertEquals(new Long(0), ctx.read("$.providers[0].regions.length()", Long.class));
  }

  @Test
  public void testSendDiagnostics() {

    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of(
                "yugaware_uuid", "0146179d-a623-4b2a-a095-bfb0062eae9f", "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    when(apiHelper.postRequest(anyString(), any(), anyMap())).thenReturn(Json.toJson(responseJson));
    callHomeManager.sendDiagnostics(defaultCustomer);

    ArgumentCaptor<String> url = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<JsonNode> params = ArgumentCaptor.forClass(JsonNode.class);
    ArgumentCaptor<Map<String, String>> headers = ArgumentCaptor.forClass(Map.class);

    verify(apiHelper).postRequest(url.capture(), params.capture(), headers.capture());
    verifyCallHome(params.getValue(), u);

    System.out.println(params.getValue());
    String expectedToken =
        Base64.getEncoder().encodeToString(defaultCustomer.getUuid().toString().getBytes());
    assertEquals(expectedToken, headers.getValue().get("X-AUTH-TOKEN"));
    assertEquals("https://yw-diagnostics.yugabyte.com", url.getValue());
  }

  @Test
  public void testNoSendDiagnostics() {
    ModelFactory.setCallhomeLevel(defaultCustomer, "NONE");
    callHomeManager.sendDiagnostics(defaultCustomer);
    verifyNoInteractions(apiHelper);
  }
}
