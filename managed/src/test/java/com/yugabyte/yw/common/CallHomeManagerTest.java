// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyCollection;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
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
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.cdc.CdcStream;
import com.yugabyte.yw.common.cdc.CdcStreamManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.CDCReplicationSlotResponse;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.forms.TableInfoForm.NamespaceInfoResp;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.helpers.TaskType;
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

  @Mock MetricQueryHelper metricQueryHelper;

  @Mock UniverseTableHandler universeTableHandler;

  @Mock CdcStreamManager cdcStreamManager;

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

    mutableConfigFactory
        .forCustomer(defaultCustomer)
        .setValue("yb.test.override", "customerOverrideValue");
    mutableConfigFactory
        .forCustomer(defaultCustomer)
        .setValue("yb.customer.region", "customerRegionValue");
    mutableConfigFactory
        .forCustomer(defaultCustomer)
        .setValue("yb.security.secret", "customerSecretValue");

    mutableConfigFactory
        .forProvider(defaultProvider)
        .setValue("yb.provider.region", "providerRegionValue");
    mutableConfigFactory.forProvider(defaultProvider).setValue("yb.provider.key", "providerValue");
    mutableConfigFactory
        .forProvider(defaultProvider)
        .setValue("yb.security.ldap.ldap_service_account_password", "providerSecretValue");

    mutableConfigFactory.forUniverse(u).setValue("yb.universe.region", "universeRegionValue");
    mutableConfigFactory.forUniverse(u).setValue("yb.universe.key", "universeValue");

    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.test.override", "globalShouldBeFilteredOut");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.global.region", "globalRegionValue");
    mutableConfigFactory.globalRuntimeConf().setValue("yb.global.key", "globalValue");
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.ldap.ldap_service_account_password", "globalSecretValue");

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

  @Test
  public void testUniverseDiagnosticsVmMetricsAndHealthStatus() {
    Universe okUniverse = ModelFactory.createUniverse("ok", defaultCustomer.getId());
    Universe warnUniverse = ModelFactory.createUniverse("warn", defaultCustomer.getId());
    Universe errUniverse = ModelFactory.createUniverse("err", defaultCustomer.getId());

    HealthCheck.Details ok = new HealthCheck.Details();
    ok.setHasError(false);
    ok.setHasWarning(false);
    HealthCheck.Details warn = new HealthCheck.Details();
    warn.setHasError(false);
    warn.setHasWarning(true);
    HealthCheck.Details err = new HealthCheck.Details();
    err.setHasError(true);
    HealthCheck.addAndPrune(okUniverse.getUniverseUUID(), defaultCustomer.getId(), ok);
    HealthCheck.addAndPrune(warnUniverse.getUniverseUUID(), defaultCustomer.getId(), warn);
    HealthCheck.addAndPrune(errUniverse.getUniverseUUID(), defaultCustomer.getId(), err);

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(inv -> RuntimeConfigEntry.getAll(inv.getArgument(0, java.util.Set.class)));
    when(universeTableHandler.listTables(
            any(), any(), anyBoolean(), anyBoolean(), anyBoolean(), anyBoolean()))
        .thenReturn(ImmutableList.of(mock(TableInfoResp.class), mock(TableInfoResp.class)));
    when(universeTableHandler.listNamespaces(any(), any(), anyBoolean()))
        .thenReturn(ImmutableList.of(mock(NamespaceInfoResp.class)));
    String vmResponse =
        "{\"cpu_usage\":{\"data\":["
            + "{\"name\":\"User\",\"y\":[\"3.3\"]},"
            + "{\"name\":\"System\",\"y\":[\"2.5\"]},"
            + "{\"name\":\"Total\",\"y\":[\"6.6\"]}]},"
            + "\"disk_usage\":{\"data\":["
            + "{\"name\":\"free\",\"y\":[\"50.0\"]},"
            + "{\"name\":\"size\",\"y\":[\"100.0\"]}]},"
            + "\"memory_usage\":{\"data\":["
            + "{\"name\":\"Total\",\"y\":[\"16.0\"]},"
            + "{\"name\":\"Free\",\"y\":[\"4.0\"]},"
            + "{\"name\":\"Cached\",\"y\":[\"2.0\"]},"
            + "{\"name\":\"Buffered\",\"y\":[\"1.0\"]}]},"
            + "\"ysql_server_rpc_per_second\":{\"data\":["
            + "{\"name\":\"Select\",\"y\":[\"10.0\"]},"
            + "{\"name\":\"Insert\",\"y\":[\"5.0\"]},"
            + "{\"name\":\"Update\",\"y\":[\"2.0\"]}]},"
            + "\"cql_server_rpc_per_second\":{\"data\":["
            + "{\"name\":\"Select\",\"y\":[\"8.0\"]},"
            + "{\"name\":\"Insert\",\"y\":[\"3.0\"]}]}}";
    when(metricQueryHelper.query(any(Customer.class), any(MetricQueryParams.class)))
        .thenReturn(Json.parse(vmResponse));

    JsonNode diagArray =
        callHomeManager
            .collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW)
            .get("universe_diagnostics");

    assertEquals(3, diagArray.size());
    for (JsonNode diag : diagArray) {
      String uuid = diag.get("universe_uuid").asText();
      if (okUniverse.getUniverseUUID().toString().equals(uuid))
        assertEquals("OK", diag.get("health_check_last_status").asText());
      if (warnUniverse.getUniverseUUID().toString().equals(uuid))
        assertEquals("WARNING", diag.get("health_check_last_status").asText());
      if (errUniverse.getUniverseUUID().toString().equals(uuid))
        assertEquals("ERROR", diag.get("health_check_last_status").asText());

      JsonNode m = diag.get("universe_metrics");
      assertEquals(3.3, m.get("cpu_user").asDouble(), 0.01);
      assertEquals(2.5, m.get("cpu_system").asDouble(), 0.01);
      assertEquals(6.6, m.get("cpu_total").asDouble(), 0.01);
      assertEquals(50.0, m.get("disk_used_gb").asDouble(), 0.01);
      assertEquals(100.0, m.get("disk_total_gb").asDouble(), 0.01);
      assertEquals(50.0, m.get("disk_usage_percent").asDouble(), 0.01);
      assertEquals(10.0, m.get("ysql_read_ops").asDouble(), 0.01);
      assertEquals(7.0, m.get("ysql_write_ops").asDouble(), 0.01);
      assertEquals(8.0, m.get("ycql_read_ops").asDouble(), 0.01);
      assertEquals(3.0, m.get("ycql_write_ops").asDouble(), 0.01);
      assertEquals(16.0, m.get("memory_total_gb").asDouble(), 0.01);
      assertEquals(4.0, m.get("memory_free_gb").asDouble(), 0.01);
      assertEquals(2.0, m.get("memory_cached_gb").asDouble(), 0.01);
      assertEquals(1.0, m.get("memory_buffered_gb").asDouble(), 0.01);
      assertEquals(2, diag.get("num_tables").asInt());
      assertEquals(1, diag.get("num_databases").asInt());
    }
  }

  @Test
  public void testUniverseDiagnostics2() {
    // k8s universe case
    Universe k8sUniverse =
        ModelFactory.createUniverse(
            "k8s", UUID.randomUUID(), defaultCustomer.getId(), Common.CloudType.kubernetes);
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(inv -> RuntimeConfigEntry.getAll(inv.getArgument(0, java.util.Set.class)));
    String k8sResponse =
        "{\"container_cpu_usage\":{\"data\":["
            + "{\"name\":\"cpu_usage\",\"y\":[\"45.0\"]}]},"
            + "\"container_volume_stats\":{\"data\":["
            + "{\"name\":\"used\",\"y\":[\"30.0\"]}]},"
            + "\"container_volume_max_usage\":{\"data\":["
            + "{\"name\":\"used\",\"y\":[\"100.0\"]}]},"
            + "\"container_memory_usage\":{\"data\":["
            + "{\"name\":\"memory_usage\",\"y\":[\"8.0\"]}]},"
            + "\"ysql_server_rpc_per_second\":{\"data\":["
            + "{\"name\":\"Select\",\"y\":[\"10.0\"]},"
            + "{\"name\":\"Insert\",\"y\":[\"5.0\"]}]},"
            + "\"cql_server_rpc_per_second\":{\"data\":[]}}";
    when(metricQueryHelper.query(any(Customer.class), any(MetricQueryParams.class)))
        .thenReturn(Json.parse(k8sResponse));
    JsonNode diagArray =
        callHomeManager
            .collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW)
            .get("universe_diagnostics");

    assertEquals(1, diagArray.size());
    JsonNode diag = diagArray.get(0);
    JsonNode m = diag.get("universe_metrics");
    assertEquals(k8sUniverse.getUniverseUUID().toString(), diag.get("universe_uuid").asText());
    assertEquals(45.0, m.get("cpu_total").asDouble(), 0.01);
    assertNull(m.get("cpu_user"));
    assertEquals(30.0, m.get("disk_used_gb").asDouble(), 0.01);
    assertEquals(100.0, m.get("disk_total_gb").asDouble(), 0.01);
    assertEquals(30.0, m.get("disk_usage_percent").asDouble(), 0.01);
    assertEquals(10.0, m.get("ysql_read_ops").asDouble(), 0.01);
    assertEquals(5.0, m.get("ysql_write_ops").asDouble(), 0.01);
    assertEquals(0.0, m.get("ycql_read_ops").asDouble(), 0.01);
    assertEquals(0.0, m.get("ycql_write_ops").asDouble(), 0.01);
    assertEquals(8.0, m.get("memory_used_gb").asDouble(), 0.01);
  }

  @Test
  public void testSendPlatformDiagnosticsSkipsWhenCallhomeDisabled() {
    ModelFactory.setCallhomeLevel(defaultCustomer, "NONE");
    callHomeManager.sendPlatformDiagnostics();
    verifyNoInteractions(apiHelper);
  }

  @Test
  public void testCollectPlatformDiagnostics() {
    String yugawareUuid = "0146179d-a623-4b2a-a095-bfb0062eae9f";
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(ImmutableMap.of("yugaware_uuid", yugawareUuid, "version", "2.25.0"));
    when(clock.instant()).thenReturn(Instant.parse("2024-03-25T10:00:00.000Z"));
    when(mockRuntimeConf.getGlobalConfValues(any())).thenReturn(Collections.emptyMap());

    JsonNode payload = callHomeManager.collectPlatformDiagnostics();
    assertEquals("platform", payload.get("payload_type").asText());
    assertEquals(yugawareUuid, payload.get("yugaware_uuid").asText());
    assertEquals("2.25.0", payload.get("yba_version").asText());
    assertEquals(
        Instant.parse("2024-03-25T10:00:00.000Z").getEpochSecond(),
        payload.get("timestamp").asLong());
    assertTrue(payload.get("customer_uuids").isArray());
    assertEquals(1, payload.get("customer_uuids").size());
    assertEquals(
        defaultCustomer.getUuid().toString(), payload.get("customer_uuids").get(0).asText());
    assertFalse(payload.get("is_yba_ha_enabled").asBoolean());
    assertEquals(0, payload.get("hostname_of_standby_yba_ha_instances").size());
    assertEquals(0, payload.get("yba_backup_history").size());
    assertEquals(0, payload.get("yba_restore_history").size());
    assertFalse(payload.get("is_user_auth_via_oidc_configured").asBoolean());
    assertFalse(payload.get("is_user_auth_via_ldap").asBoolean());
    assertTrue(payload.get("oidc_config").isNull());
    assertTrue(payload.get("ldap_config").isNull());

    HighAvailabilityConfig haConfig = HighAvailabilityConfig.create("test-cluster-key");
    PlatformInstance.create(haConfig, "http://local.yba.example.com", true, true);
    PlatformInstance.create(haConfig, "http://standby.yba.example.com", false, false);
    JsonNode haPayload = callHomeManager.collectPlatformDiagnostics();
    assertTrue(haPayload.get("is_yba_ha_enabled").asBoolean());
    JsonNode standbys = haPayload.get("hostname_of_standby_yba_ha_instances");
    assertEquals(1, standbys.size());
    assertEquals("http://standby.yba.example.com", standbys.get(0).asText());
  }

  @Test
  public void testSendPlatformDiagnostics() {
    String yugawareUuid = "0146179d-a623-4b2a-a095-bfb0062eae9f";
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(ImmutableMap.of("yugaware_uuid", yugawareUuid, "version", "2.25.0"));
    when(clock.instant()).thenReturn(Instant.parse("2024-03-25T10:00:00.000Z"));
    when(mockRuntimeConf.getGlobalConfValues(any())).thenReturn(Collections.emptyMap());

    ObjectNode responseJson = Json.newObject();
    responseJson.put("success", true);
    when(apiHelper.postRequest(anyString(), any(), any())).thenReturn(Json.toJson(responseJson));

    callHomeManager.sendPlatformDiagnostics();

    ArgumentCaptor<String> url = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<JsonNode> params = ArgumentCaptor.forClass(JsonNode.class);
    ArgumentCaptor<Map<String, String>> headers = ArgumentCaptor.forClass(Map.class);

    verify(apiHelper).postRequest(url.capture(), params.capture(), headers.capture());

    assertEquals("https://diagnostics.yugabyte.com", url.getValue());
    assertEquals("platform", params.getValue().get("payload_type").asText());
    assertEquals(yugawareUuid, params.getValue().get("yugaware_uuid").asText());
    assertNull(headers.getValue());
  }

  @Test
  public void testUniverseCdcEnrichment() throws Exception {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(inv -> RuntimeConfigEntry.getAll(inv.getArgument(0, java.util.Set.class)));

    // CDC not configured
    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);
    JsonNode universeNode = payload.get("universes").get(0);
    assertFalse(universeNode.get("is_cdc_configured").asBoolean());
    assertEquals("", universeNode.get("cdc_replication_slots").asText());

    //  CDC configured with slots
    CdcStream stream = new CdcStream("stream-id-1", Collections.emptyMap(), "ns-1");
    when(cdcStreamManager.getAllCdcStreams(any(Universe.class)))
        .thenReturn(ImmutableList.of(stream));

    CDCReplicationSlotResponse slotResp = new CDCReplicationSlotResponse();
    CDCReplicationSlotResponse.CDCReplicationSlotDetails slot1 =
        new CDCReplicationSlotResponse.CDCReplicationSlotDetails();
    slot1.slotName = "slot_a";
    CDCReplicationSlotResponse.CDCReplicationSlotDetails slot2 =
        new CDCReplicationSlotResponse.CDCReplicationSlotDetails();
    slot2.slotName = "slot_b";
    slotResp.replicationSlots = ImmutableList.of(slot1, slot2);
    when(cdcStreamManager.listReplicationSlot(any(Universe.class))).thenReturn(slotResp);

    payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);
    universeNode = payload.get("universes").get(0);
    assertTrue(universeNode.get("is_cdc_configured").asBoolean());
    assertEquals("slot_a,slot_b", universeNode.get("cdc_replication_slots").asText());

    // Exception case
    when(cdcStreamManager.getAllCdcStreams(any(Universe.class)))
        .thenThrow(new RuntimeException("simulated failure"));

    payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);
    universeNode = payload.get("universes").get(0);
    assertTrue(universeNode.get("is_cdc_configured").isNull());
    assertTrue(universeNode.get("cdc_replication_slots").isNull());
  }

  @Test
  public void testTasksPayloadIncludesTaskParams() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(inv -> RuntimeConfigEntry.getAll(inv.getArgument(0, java.util.Set.class)));

    ObjectNode taskParamsJson = Json.newObject();
    taskParamsJson.put("ybSoftwareVersion", "2.29.0.0-b369");
    taskParamsJson.put("ybPrevSoftwareVersion", "2025.2.0.1-b1");
    taskParamsJson.put("ysqlPassword", "mysecret");

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setTaskParams(taskParamsJson);
    taskInfo.setOwner("test-owner");
    taskInfo.setTaskState(TaskInfo.State.Success);
    taskInfo.save();

    CustomerTask ct =
        CustomerTask.create(
            defaultCustomer,
            u.getUniverseUUID(),
            taskInfo.getUuid(),
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.SoftwareUpgrade,
            u.getName());
    ct.markAsCompleted();

    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    JsonNode tasks = payload.get("tasks");
    assertNotNull(tasks);
    assertEquals(1, tasks.size());

    JsonNode task = tasks.get(0);
    assertEquals("SoftwareUpgradeYB", task.get("task_name").asText());
    assertEquals("Universe", task.get("target_type").asText());
    assertEquals(u.getUniverseUUID().toString(), task.get("target_uuid").asText());
    assertEquals("Success", task.get("task_state").asText());
    assertFalse(task.get("task_params").isNull());
    assertEquals("2.29.0.0-b369", task.get("task_params").get("ybSoftwareVersion").asText());
    assertEquals("2025.2.0.1-b1", task.get("task_params").get("ybPrevSoftwareVersion").asText());
    assertEquals("REDACTED", task.get("task_params").get("ysqlPassword").asText());
  }

  @Test
  public void testScheduledBackupPoliciesAndCpuMemoryDiag() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    CustomerConfig storageConfig =
        ModelFactory.createS3StorageConfig(defaultCustomer, "test-storage");
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), storageConfig.getConfigUUID());

    UUID providerUUID =
        UUID.fromString(u.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    InstanceType it =
        InstanceType.upsert(
            providerUUID, "m3.medium", 4.0, 8.0, new InstanceType.InstanceTypeDetails());
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams.UserIntent intent =
              univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = it.getInstanceTypeCode();
          intent.numNodes = 3;
        });

    when(configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata))
        .thenReturn(
            ImmutableMap.of("yugaware_uuid", UUID.randomUUID().toString(), "version", "0.0.1"));
    when(mockRuntimeConf.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    when(clock.instant()).thenReturn(Instant.parse("2019-01-24T18:46:07.517Z"));
    when(runtimeConfService.getRuntimeConfigEntries(anySet()))
        .thenAnswer(inv -> RuntimeConfigEntry.getAll(inv.getArgument(0, java.util.Set.class)));

    JsonNode payload =
        callHomeManager.collectDiagnostics(defaultCustomer, CallHomeManager.CollectionLevel.LOW);

    JsonNode universeNode = payload.get("universes").get(0);
    JsonNode backupPolicies = universeNode.get("scheduled_backup_policies");
    assertNotNull(backupPolicies);
    assertTrue(backupPolicies.isArray());
    assertEquals(1, backupPolicies.size());
    assertEquals(
        schedule.getScheduleUUID().toString(), backupPolicies.get(0).get("scheduleUUID").asText());

    JsonNode diag = payload.get("universe_diagnostics").get(0);
    assertEquals(u.getUniverseUUID().toString(), diag.get("universe_uuid").asText());
    assertEquals(12, diag.get("total_cpu_cores").asInt());
    assertEquals(24.0, diag.get("total_mem_gb").asDouble(), 0.01);
  }
}
