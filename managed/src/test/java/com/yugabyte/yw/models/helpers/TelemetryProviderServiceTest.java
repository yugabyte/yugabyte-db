// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.*;
import static org.mockito.Mockito.doNothing;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.GCPCloudMonitoringConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class TelemetryProviderServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;
  private TelemetryProviderService telemetryProviderService;

  @Mock WSClientRefresher wsClientRefresher;
  @Mock BeanValidator beanValidator;
  @Mock BeanValidator.ErrorMessageBuilder errorMessageBuilder;
  @Mock RuntimeConfGetter mockConfGetter;

  @Before
  public void setUp() {
    // Initialize mocks manually since we're using JUnitParamsRunner
    MockitoAnnotations.openMocks(this);

    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();

    // Mock the BeanValidator and its ErrorMessageBuilder
    doNothing().when(beanValidator).validate(any());
    when(beanValidator.error()).thenReturn(errorMessageBuilder);
    when(errorMessageBuilder.forField(anyString(), anyString())).thenReturn(errorMessageBuilder);

    // Mock throwError to actually throw the expected exception
    doAnswer(
            invocation -> {
              Map<String, List<String>> errors = new HashMap<>();
              errors.put("name", Arrays.asList("provider with such name already exists."));
              JsonNode errJson = Json.toJson(errors);
              throw new PlatformServiceException(BAD_REQUEST, errJson);
            })
        .when(errorMessageBuilder)
        .throwError();

    // Mock WSClientRefresher to avoid the getApiHelper issue
    when(wsClientRefresher.getClient(anyString())).thenReturn(null);

    // Create a real service instance with mocked dependencies
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.skipTPsCredsConsistencyCheck)))
        .thenReturn(false);
    telemetryProviderService =
        new TelemetryProviderService(beanValidator, mockConfGetter, wsClientRefresher);

    // Create a spy to override only the problematic getApiHelper method
    telemetryProviderService = spy(telemetryProviderService);
    doReturn(mockApiHelper).when(telemetryProviderService).getApiHelper();

    // Mock the API helper responses
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(Json.parse("{}"));
  }

  @Parameters({"DataDog", "Splunk", "AWSCloudWatch", "GCPCloudMonitoring"})
  @Test
  public void testSerialization(String jsonFileName) throws IOException {

    String initial = TestUtils.readResource("telemetry/" + jsonFileName + ".json");

    JsonNode initialJson = Json.parse(initial);

    TelemetryProvider settings = Json.fromJson(initialJson, TelemetryProvider.class);

    JsonNode resultJson = Json.toJson(settings);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testCreateAndGet() {
    TelemetryProvider provider = createTestProvider("test");
    TelemetryProvider updated = telemetryProviderService.save(provider);

    assertThat(updated, equalTo(provider));

    TelemetryProvider fromDb = telemetryProviderService.get(provider.getUuid());
    assertThat(fromDb, equalTo(provider));
  }

  @Test
  public void testGetOrBadRequest() {
    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              telemetryProviderService.getOrBadRequest(uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Telemetry Provider UUID: " + uuid));
  }

  @Test
  public void testListByCustomerUuid() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    TelemetryProvider provider2 = createTestProvider("test2");
    telemetryProviderService.save(provider2);

    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    TelemetryProvider otherCustomerProvider = createTestProvider(newCustomerUUID, "test2");

    List<TelemetryProvider> providers = telemetryProviderService.list(defaultCustomerUuid);
    assertThat(providers, containsInAnyOrder(provider, provider2));
  }

  @Test
  public void testValidateDuplicateName() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    TelemetryProvider duplicate = createTestProvider("test");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              telemetryProviderService.save(duplicate);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"provider with such name already exists.\"]}"));
  }

  @Test
  public void testDelete() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    telemetryProviderService.delete(provider.getUuid());

    TelemetryProvider fromDb = telemetryProviderService.get(provider.getUuid());
    assertThat(fromDb, nullValue());
  }

  @Test
  public void testAreCredentialsConsistent_MultipleAWSProviders_SameCredentials() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider3 =
        createAWSTelemetryProvider("aws-provider-3", "access-key-1", "secret-key-1");
    List<TelemetryProvider> providers = Arrays.asList(awsProvider1, awsProvider2, awsProvider3);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertTrue(result);
  }

  @Test
  public void testAreCredentialsConsistent_MultipleAWSProviders_DifferentAccessKey() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-2", "secret-key-1");
    List<TelemetryProvider> providers = Arrays.asList(awsProvider1, awsProvider2);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertFalse(result);
  }

  @Test
  public void testAreCredentialsConsistent_MultipleAWSProviders_DifferentSecretKey() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-1", "secret-key-2");
    List<TelemetryProvider> providers = Arrays.asList(awsProvider1, awsProvider2);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertFalse(result);
  }

  @Test
  public void testAreCredentialsConsistent_MultipleGCPProviders_SameCredentials() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    JsonNode credentials = createTestGCPCredentials();
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", credentials);
    TelemetryProvider gcpProvider2 = createGCPTelemetryProvider("gcp-provider-2", credentials);
    TelemetryProvider gcpProvider3 = createGCPTelemetryProvider("gcp-provider-3", credentials);
    List<TelemetryProvider> providers = Arrays.asList(gcpProvider1, gcpProvider2, gcpProvider3);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertTrue(result);
  }

  @Test
  public void testAreCredentialsConsistent_MultipleGCPProviders_DifferentCredentials() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    JsonNode credentials1 = createTestGCPCredentials("project-1");
    JsonNode credentials2 = createTestGCPCredentials("project-2");
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", credentials1);
    TelemetryProvider gcpProvider2 = createGCPTelemetryProvider("gcp-provider-2", credentials2);
    List<TelemetryProvider> providers = Arrays.asList(gcpProvider1, gcpProvider2);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertFalse(result);
  }

  @Test
  public void testAreCredentialsConsistent_MixedProviderTypes() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-1", "secret-key-1");
    JsonNode credentials = createTestGCPCredentials();
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", credentials);
    TelemetryProvider gcpProvider2 = createGCPTelemetryProvider("gcp-provider-2", credentials);
    List<TelemetryProvider> providers =
        Arrays.asList(awsProvider1, awsProvider2, gcpProvider1, gcpProvider2);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertTrue(result);
  }

  @Test
  public void testAreCredentialsConsistent_MixedProviderTypes_WithInconsistencies() {
    Universe universe = ModelFactory.createUniverse("test-universe");
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-2", "secret-key-1");
    JsonNode credentials1 = createTestGCPCredentials("project-1");
    JsonNode credentials2 = createTestGCPCredentials("project-2");
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", credentials1);
    TelemetryProvider gcpProvider2 = createGCPTelemetryProvider("gcp-provider-2", credentials2);
    List<TelemetryProvider> providers =
        Arrays.asList(awsProvider1, awsProvider2, gcpProvider1, gcpProvider2);

    boolean result = telemetryProviderService.areCredentialsConsistent(universe, providers);
    assertFalse(result);
  }

  @Test
  public void testAreTPsCredentialsConsistentOnUniverse_WithConsistentCredentials() {
    // Create telemetry providers with consistent credentials
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    telemetryProviderService.save(awsProvider1);
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-1", "secret-key-1");
    telemetryProviderService.save(awsProvider2);
    JsonNode gcpCredentials = createTestGCPCredentials();
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", gcpCredentials);
    telemetryProviderService.save(gcpProvider1);
    TelemetryProvider gcpProvider2 = createGCPTelemetryProvider("gcp-provider-2", gcpCredentials);
    telemetryProviderService.save(gcpProvider2);

    // Create universe with all three export configs
    Universe universe = ModelFactory.createUniverse("test-universe");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams params = universe.getUniverseDetails();
                UniverseDefinitionTaskParams.UserIntent userIntent =
                    params.getPrimaryCluster().userIntent;

                // Set up audit log config
                AuditLogConfig auditLogConfig = new AuditLogConfig();
                List<UniverseLogsExporterConfig> auditExporterConfigs = new ArrayList<>();
                UniverseLogsExporterConfig auditExporter1 = new UniverseLogsExporterConfig();
                auditExporter1.setExporterUuid(awsProvider1.getUuid());
                auditExporterConfigs.add(auditExporter1);
                auditLogConfig.setUniverseLogsExporterConfig(auditExporterConfigs);
                userIntent.auditLogConfig = auditLogConfig;

                // Set up query log config
                QueryLogConfig queryLogConfig = new QueryLogConfig();
                List<UniverseQueryLogsExporterConfig> queryExporterConfigs = new ArrayList<>();
                UniverseQueryLogsExporterConfig queryExporter1 =
                    new UniverseQueryLogsExporterConfig();
                queryExporter1.setExporterUuid(awsProvider2.getUuid());
                queryExporterConfigs.add(queryExporter1);
                queryLogConfig.setUniverseLogsExporterConfig(queryExporterConfigs);
                userIntent.queryLogConfig = queryLogConfig;

                // Set up metrics export config
                MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
                List<UniverseMetricsExporterConfig> metricsExporterConfigs = new ArrayList<>();
                UniverseMetricsExporterConfig metricsExporter1 =
                    new UniverseMetricsExporterConfig();
                metricsExporter1.setExporterUuid(gcpProvider1.getUuid());
                metricsExporterConfigs.add(metricsExporter1);
                UniverseMetricsExporterConfig metricsExporter2 =
                    new UniverseMetricsExporterConfig();
                metricsExporter2.setExporterUuid(gcpProvider2.getUuid());
                metricsExporterConfigs.add(metricsExporter2);
                metricsExportConfig.setUniverseMetricsExporterConfig(metricsExporterConfigs);
                userIntent.metricsExportConfig = metricsExportConfig;

                params.getPrimaryCluster().userIntent = userIntent;
                universe.setUniverseDetails(params);
              }
            },
            false);

    boolean result = telemetryProviderService.areTPsCredentialsConsistentOnUniverse(universe);
    assertTrue(result);
  }

  @Test
  public void testAreTPsCredentialsConsistentOnUniverse_WithInconsistentCredentials() {
    // Create telemetry providers with inconsistent credentials
    TelemetryProvider awsProvider1 =
        createAWSTelemetryProvider("aws-provider-1", "access-key-1", "secret-key-1");
    telemetryProviderService.save(awsProvider1);
    TelemetryProvider awsProvider2 =
        createAWSTelemetryProvider("aws-provider-2", "access-key-2", "secret-key-1");
    telemetryProviderService.save(awsProvider2);
    JsonNode gcpCredentials = createTestGCPCredentials();
    TelemetryProvider gcpProvider1 = createGCPTelemetryProvider("gcp-provider-1", gcpCredentials);
    telemetryProviderService.save(gcpProvider1);

    // Create universe with all three export configs
    Universe universe = ModelFactory.createUniverse("test-universe");
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams params = universe.getUniverseDetails();
                UniverseDefinitionTaskParams.UserIntent userIntent =
                    params.getPrimaryCluster().userIntent;

                // Set up audit log config with AWS provider
                AuditLogConfig auditLogConfig = new AuditLogConfig();
                List<UniverseLogsExporterConfig> auditExporterConfigs = new ArrayList<>();
                UniverseLogsExporterConfig auditExporter1 = new UniverseLogsExporterConfig();
                auditExporter1.setExporterUuid(awsProvider1.getUuid());
                auditExporterConfigs.add(auditExporter1);
                auditLogConfig.setUniverseLogsExporterConfig(auditExporterConfigs);
                userIntent.auditLogConfig = auditLogConfig;

                // Set up query log config with AWS provider
                QueryLogConfig queryLogConfig = new QueryLogConfig();
                List<UniverseQueryLogsExporterConfig> queryExporterConfigs = new ArrayList<>();
                UniverseQueryLogsExporterConfig queryExporter1 =
                    new UniverseQueryLogsExporterConfig();
                queryExporter1.setExporterUuid(awsProvider2.getUuid());
                queryExporterConfigs.add(queryExporter1);
                queryLogConfig.setUniverseLogsExporterConfig(queryExporterConfigs);
                userIntent.queryLogConfig = queryLogConfig;

                // Set up metrics export config with GCP provider
                MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
                List<UniverseMetricsExporterConfig> metricsExporterConfigs = new ArrayList<>();
                UniverseMetricsExporterConfig metricsExporter1 =
                    new UniverseMetricsExporterConfig();
                metricsExporter1.setExporterUuid(gcpProvider1.getUuid());
                metricsExporterConfigs.add(metricsExporter1);
                metricsExportConfig.setUniverseMetricsExporterConfig(metricsExporterConfigs);
                userIntent.metricsExportConfig = metricsExportConfig;

                params.getPrimaryCluster().userIntent = userIntent;
                universe.setUniverseDetails(params);
              }
            },
            false);

    boolean result = telemetryProviderService.areTPsCredentialsConsistentOnUniverse(universe);
    assertFalse(result);
  }

  private TelemetryProvider createTestProvider(String name) {
    return createTestProvider(defaultCustomerUuid, name);
  }

  public static TelemetryProvider createTestProvider(UUID customerUUID, String name) {
    TelemetryProvider provider = new TelemetryProvider();
    provider.setName(name);
    provider.setCustomerUUID(customerUUID);
    Map<String, String> tags = new HashMap<>();
    tags.put("user1", name);
    tags.put("address", "CA");
    provider.setTags(tags);

    TelemetryProviderConfig config = new TelemetryProviderConfig();
    config.setType(ProviderType.DATA_DOG);
    if (config.getType() == ProviderType.DATA_DOG) {
      DataDogConfig dataDogConfig = new DataDogConfig();
      dataDogConfig.setApiKey("data-dog-api-key");
      dataDogConfig.setSite("us3.datadoghq.com");
      provider.setConfig(dataDogConfig);
    }
    return provider;
  }

  private TelemetryProvider createAWSTelemetryProvider(
      String name, String accessKey, String secretKey) {
    TelemetryProvider provider = new TelemetryProvider();
    provider.setUuid(UUID.randomUUID());
    provider.setCustomerUUID(defaultCustomerUuid);
    provider.setName(name);
    provider.setTags(new HashMap<>());

    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setAccessKey(accessKey);
    awsConfig.setSecretKey(secretKey);
    awsConfig.setLogGroup("test-log-group");
    awsConfig.setLogStream("test-log-stream");
    awsConfig.setRegion("us-west-2");
    provider.setConfig(awsConfig);

    return provider;
  }

  private TelemetryProvider createGCPTelemetryProvider(String name, JsonNode credentials) {
    TelemetryProvider provider = new TelemetryProvider();
    provider.setUuid(UUID.randomUUID());
    provider.setCustomerUUID(defaultCustomerUuid);
    provider.setName(name);
    provider.setTags(new HashMap<>());

    GCPCloudMonitoringConfig gcpConfig = new GCPCloudMonitoringConfig();
    gcpConfig.setProject("test-project");
    gcpConfig.setCredentials(credentials);
    provider.setConfig(gcpConfig);

    return provider;
  }

  private JsonNode createTestGCPCredentials() {
    return createTestGCPCredentials("test-project");
  }

  private JsonNode createTestGCPCredentials(String projectId) {
    Map<String, String> credentialsMap = new HashMap<>();
    credentialsMap.put("type", "service_account");
    credentialsMap.put("project_id", projectId);
    credentialsMap.put("private_key_id", "test-key-id");
    credentialsMap.put(
        "private_key", "-----BEGIN PRIVATE KEY-----\ntest-key\n-----END PRIVATE KEY-----\n");
    credentialsMap.put("client_email", "test@test-project.iam.gserviceaccount.com");
    return Json.toJson(credentialsMap);
  }
}
