// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyCollection;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.time.Clock;
import java.time.Instant;
import java.util.Base64;
import java.util.Collections;
import java.util.Date;
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

  @Mock RuntimeConfService runtimeConfService;

  @Mock AlertConfigurationService alertConfigurationService;

  @Mock AlertDestinationService alertDestinationService;

  @Mock AlertChannelService alertChannelService;

  @Mock AlertService alertService;

  @Mock XClusterUniverseService xClusterUniverseService;

  Customer defaultCustomer;
  Users defaultUser;
  Provider defaultProvider;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);

    when(alertConfigurationService.listByCustomerUuid(any(UUID.class)))
        .thenReturn(ImmutableList.of());
    when(alertDestinationService.listByCustomer(any(UUID.class))).thenReturn(ImmutableList.of());
    when(alertChannelService.list(any(UUID.class))).thenReturn(ImmutableList.of());
    when(alertService.listByCustomerSince(any(UUID.class), any(Date.class)))
        .thenReturn(ImmutableList.of());
    when(xClusterUniverseService.getXClusterConfigsByUuids(anyCollection()))
        .thenReturn(Collections.emptyMap());
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
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));
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
    assertEquals("https://diagnostics.yugabyte.com", url.getValue());
  }

  @Test
  public void testNoSendDiagnostics() {
    ModelFactory.setCallhomeLevel(defaultCustomer, "NONE");
    callHomeManager.sendDiagnostics(defaultCustomer);
    verifyNoInteractions(apiHelper);
  }

  @Test
  public void testOnPremProviderEnrichmentFields() {

    Provider onprem = ModelFactory.onpremProvider(defaultCustomer);
    ProviderDetails d = onprem.getDetails();
    d.airGapInstall = true;
    d.skipProvisioning = true;
    d.setUpChrony = true;
    d.ntpServers = ImmutableList.of("0.pool.ntp.org");
    onprem.setDetails(d);
    onprem.save();

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));

    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    assertEquals(2, payload.get("providers").size());
    JsonNode onpremProvider = null;
    for (JsonNode p : payload.get("providers")) {
      if ("onprem".equals(p.get("code").asText())) {
        onpremProvider = p;
        break;
      }
    }
    assertNotNull(onpremProvider);
    assertEquals("onprem", onpremProvider.get("code").asText());
    assertEquals(true, onpremProvider.get("is_airgap").asBoolean());
    assertEquals(true, onpremProvider.get("on_prem_manually_provision").asBoolean());
    assertEquals("SPECIFY_CUSTOM_NTP_SERVERS", onpremProvider.get("ntp_setup_detail").asText());
    assertTrue(onpremProvider.get("public_cloud_credential_type").isNull());
  }

  @Test
  public void testAwsProviderNtpAndCredentialType() {
    Provider aws = defaultProvider;
    ProviderDetails d = aws.getDetails();

    d.setUpChrony = true;
    d.ntpServers = ImmutableList.of();

    ProviderDetails.CloudInfo ci = new ProviderDetails.CloudInfo();
    AWSCloudInfo awsInfo = new AWSCloudInfo();
    awsInfo.awsAccessKeyID = "ACCESS_KEY_TEST";
    ci.aws = awsInfo;
    d.setCloudInfo(ci);
    aws.setDetails(d);
    aws.save();

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));

    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    JsonNode awsJson = null;
    for (JsonNode p : payload.get("providers")) {
      if ("aws".equals(p.get("code").asText())) {
        awsJson = p;
        break;
      }
    }
    assertNotNull(awsJson);

    assertTrue(awsJson.get("on_prem_manually_provision").isNull());
    assertEquals("USE_CLOUD_NTP_SERVER", awsJson.get("ntp_setup_detail").asText());
    assertEquals("SPECIFY_ACCESS_KEY", awsJson.get("public_cloud_credential_type").asText());
  }

  @Test
  public void testRuntimeConfigPayload() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());

    RuntimeConfigEntry.upsert(defaultCustomer, "yb.test.override", "customerOverrideValue");
    RuntimeConfigEntry.upsert(defaultCustomer, "yb.customer.region", "customerRegionValue");
    RuntimeConfigEntry.upsert(defaultCustomer, "yb.security.secret", "customerSecretValue");

    RuntimeConfigEntry.upsert(defaultProvider, "yb.provider.region", "providerRegionValue");
    RuntimeConfigEntry.upsert(defaultProvider, "yb.provider.key", "providerValue");
    RuntimeConfigEntry.upsert(
        defaultProvider, "yb.security.ldap.ldap_service_account_password", "providerSecretValue");

    RuntimeConfigEntry.upsert(u, "yb.universe.region", "universeRegionValue");
    RuntimeConfigEntry.upsert(u, "yb.universe.key", "universeValue");

    RuntimeConfigEntry.upsertGlobal("yb.test.override", "globalShouldBeFilteredOut");
    RuntimeConfigEntry.upsertGlobal("yb.global.region", "globalRegionValue");
    RuntimeConfigEntry.upsertGlobal("yb.global.key", "globalValue");
    RuntimeConfigEntry.upsertGlobal(
        "yb.security.ldap.ldap_service_account_password", "globalSecretValue");

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));

    JsonNode runtimeConfig =
        callHomeManager
            .collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW)
            .get("runtime_config");
    assertNotNull(runtimeConfig);
    assertTrue(runtimeConfig.isArray());

    String customerUuid = defaultCustomer.getUuid().toString();
    String providerUuid = defaultProvider.getUuid().toString();
    String universeUuid = u.getUniverseUUID().toString();

    JsonNode customerOverride = null;
    JsonNode customerNormal = null;
    JsonNode providerNormal = null;
    JsonNode universeNormal = null;
    JsonNode globalNormal = null;
    boolean foundFilteredGlobalOverride = false;

    for (JsonNode e : runtimeConfig) {
      String scopeType = e.get("scope_type").asText();
      String scopeUuid = e.get("scopeUUID").asText();
      String path = e.get("path").asText();
      String value = e.get("value").asText();

      if ("CUSTOMER".equals(scopeType) && customerUuid.equals(scopeUuid)) {
        if ("yb.test.override".equals(path)) {
          customerOverride = e;
          assertEquals("customerOverrideValue", value);
        } else if ("yb.customer.region".equals(path)) {
          customerNormal = e;
          assertEquals("customerRegionValue", value);
        } else if ("yb.security.secret".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        }
      } else if ("PROVIDER".equals(scopeType) && providerUuid.equals(scopeUuid)) {
        if ("yb.provider.region".equals(path)) {
          providerNormal = e;
          assertEquals("providerRegionValue", value);
        } else if ("yb.provider.key".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        } else if ("yb.security.ldap.ldap_service_account_password".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        }
      } else if ("UNIVERSE".equals(scopeType) && universeUuid.equals(scopeUuid)) {
        if ("yb.universe.region".equals(path)) {
          universeNormal = e;
          assertEquals("universeRegionValue", value);
        } else if ("yb.universe.key".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        }
      } else if ("GLOBAL".equals(scopeType)) {
        if ("yb.global.region".equals(path)) {
          globalNormal = e;
          assertEquals("globalRegionValue", value);
        } else if ("yb.global.key".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        } else if ("yb.security.ldap.ldap_service_account_password".equals(path)) {
          assertEquals(RedactingService.SECRET_REPLACEMENT, value);
        } else if ("yb.test.override".equals(path)) {
          foundFilteredGlobalOverride = true;
        }
      }
    }

    assertNotNull(customerOverride);
    assertNotNull(customerNormal);
    assertNotNull(providerNormal);
    assertNotNull(universeNormal);
    assertNotNull(globalNormal);
    assertTrue(!foundFilteredGlobalOverride);
  }

  @Test
  public void testAddAlertMetadata() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);

    Instant now = Instant.parse("2019-01-24T18:46:07.517Z");
    when(clock.instant()).thenReturn(now);

    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    ModelFactory.createAlertConfiguration(defaultCustomer, u);

    String emailName = "Email Channel A";
    String slackName = "Slack Channel B";
    String destName = "Destination 1";

    ModelFactory.createAlertDestination(
        defaultCustomer.getUuid(),
        destName,
        ImmutableList.of(
            ModelFactory.createEmailChannel(defaultCustomer.getUuid(), emailName),
            ModelFactory.createSlackChannel(defaultCustomer.getUuid(), slackName)));

    ModelFactory.createAlert(defaultCustomer, u);

    when(alertConfigurationService.listByCustomerUuid(any(UUID.class)))
        .thenAnswer(
            inv ->
                AlertConfiguration.createQueryByFilter(
                        AlertConfigurationFilter.builder()
                            .customerUuid(inv.getArgument(0, UUID.class))
                            .build())
                    .findList());

    when(alertDestinationService.listByCustomer(any(UUID.class))).thenCallRealMethod();
    when(alertChannelService.list(any(UUID.class))).thenCallRealMethod();
    when(alertService.listByCustomerSince(any(UUID.class), any(Date.class))).thenCallRealMethod();
    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    JsonNode policies = payload.get("alert_policies");
    assertNotNull(policies);
    assertTrue(policies.isArray());
    assertEquals(2, policies.size());
    assertEquals("MEMORY_CONSUMPTION", policies.get(0).get("template").asText());

    JsonNode channels = payload.get("alert_notification_channels");
    assertNotNull(channels);
    assertEquals(2, channels.size());

    boolean hasEmail = false;
    boolean hasSlack = false;
    for (JsonNode ch : channels) {
      assertTrue(ch.hasNonNull("name"));
      assertTrue(ch.hasNonNull("type"));
      if (emailName.equals(ch.get("name").asText())) {
        assertEquals("Email", ch.get("type").asText());
        hasEmail = true;
      } else if (slackName.equals(ch.get("name").asText())) {
        assertEquals("Slack", ch.get("type").asText());
        hasSlack = true;
      }
    }
    assertTrue(hasEmail);
    assertTrue(hasSlack);

    JsonNode destinations = payload.get("alert_destinations");
    assertNotNull(destinations);
    assertTrue(destinations.isArray());
    assertEquals(1, destinations.size());
    assertEquals(destName, destinations.get(0).get("name").asText());

    JsonNode alertList = payload.get("alert_list");
    assertNotNull(alertList);
    assertTrue(alertList.isArray());
    assertTrue(alertList.size() >= 1);
    assertEquals("Alert 1", alertList.get(0).get("name").asText());
  }

  @Test
  public void testUniverseXClusterEnrichment() {
    Universe testUniverse = ModelFactory.createUniverse("test-univ", defaultCustomer.getId());
    Universe otherUniverse = ModelFactory.createUniverse("other-univ", defaultCustomer.getId());

    XClusterConfig plainConfig =
        XClusterConfig.create(
            "plain-xcluster", testUniverse.getUniverseUUID(), otherUniverse.getUniverseUUID());

    XClusterConfig drBackedConfig =
        XClusterConfig.create(
            "dr-xcluster", otherUniverse.getUniverseUUID(), testUniverse.getUniverseUUID());

    DrConfig drConfig = new DrConfig();
    drConfig.setUuid(UUID.randomUUID());
    drConfig.setName("test-dr-config");
    drConfig.setCreateTime(new Date());
    drConfig.setModifyTime(new Date());
    drConfig.setState(DrConfigStates.State.Replicating);
    drConfig.setPitrRetentionPeriodSec(86400L);
    drConfig.setPitrSnapshotIntervalSec(3600L);
    drConfig.save();

    drBackedConfig.setDrConfig(drConfig);
    drBackedConfig.save();

    when(xClusterUniverseService.getXClusterConfigsByUuids(anyCollection()))
        .thenReturn(
            ImmutableMap.of(
                plainConfig.getUuid(), plainConfig,
                drBackedConfig.getUuid(), drBackedConfig));

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(
            invocation ->
                RuntimeConfigEntry.getAll(invocation.getArgument(0, java.util.Set.class)));

    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    JsonNode testUniverseNode = null;
    for (JsonNode n : payload.get("universes")) {
      if ("test-univ".equals(n.get("name").asText())) {
        testUniverseNode = n;
        break;
      }
    }

    assertNotNull(testUniverseNode);
    assertEquals(true, testUniverseNode.get("is_xcluster_repl_configured").asBoolean());

    JsonNode xclusterSettings = testUniverseNode.get("xclusterSettings");
    assertNotNull(xclusterSettings);

    assertEquals(1, xclusterSettings.get("asSource").size());
    JsonNode plainNode = xclusterSettings.get("asSource").get(0);
    assertTrue(plainNode.get("dr_config") == null || plainNode.get("dr_config").isNull());

    assertEquals(1, xclusterSettings.get("asTarget").size());
    JsonNode drNode = xclusterSettings.get("asTarget").get(0);
    JsonNode drConfigNode = drNode.get("dr_config");
    assertNotNull(drConfigNode);
    assertEquals(drConfig.getUuid().toString(), drConfigNode.get("uuid").asText());
    assertEquals("test-dr-config", drConfigNode.get("name").asText());
    assertEquals("Replicating", drConfigNode.get("state").asText());
  }
}
