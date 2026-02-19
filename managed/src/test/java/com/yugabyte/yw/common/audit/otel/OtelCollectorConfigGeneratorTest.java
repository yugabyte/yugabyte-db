// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.audit.otel;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.yaml.SkipNullRepresenter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.YSQLQueryLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.*;
import com.yugabyte.yw.models.helpers.telemetry.AuthCredentials.AuthType;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.yaml.snakeyaml.Yaml;
import play.libs.Json;

public class OtelCollectorConfigGeneratorTest extends FakeDBApplication {

  private static String OTEL_COL_TMP_PATH = "/tmp/otel/";

  private OtelCollectorConfigGenerator generator;
  private Customer customer;
  private Provider provider;
  private Universe universe;
  private NodeTaskParams nodeTaskParams;

  @Before
  public void setUp() {
    new File(OTEL_COL_TMP_PATH).mkdir();
    generator = app.injector().instanceOf(OtelCollectorConfigGenerator.class);

    // Configure mockTelemetryProviderService with proper behavior
    doNothing().when(mockTelemetryProviderService).validateBean(any());

    // Configure save to return the input provider (like a real save would)
    when(mockTelemetryProviderService.save(any(TelemetryProvider.class)))
        .thenAnswer(invocation -> invocation.getArgument(0));

    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    universe =
        ModelFactory.createUniverse(
            "test-universe", UUID.fromString("00000000-0000-0000-0000-000000000000"));
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    // update the node name and set ports
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams params = universe.getUniverseDetails();
                for (NodeDetails node : params.nodeDetailsSet) {
                  node.nodeName = "test-node";
                  node.masterHttpPort = 7000;
                  node.tserverHttpPort = 9000;
                  node.ysqlServerHttpPort = 13000;
                  node.yqlServerHttpPort = 12000;
                  node.nodeExporterPort = 9300;
                  node.otelCollectorMetricsPort = 8889;
                  node.cloudInfo.region = "";
                  node.cloudInfo.az = "";
                  node.azUuid = null;
                }
                universe.setUniverseDetails(params);
              }
            },
            false);
    // create nodeTaskParams
    nodeTaskParams = new NodeTaskParams();
    nodeTaskParams.setUniverseUUID(universe.getUniverseUUID());
    nodeTaskParams.nodeName = "test-node";
  }

  @After
  public void tearDown() throws IOException {
    File file = new File(OTEL_COL_TMP_PATH);
    FileUtils.deleteDirectory(file);
  }

  // Helper method to create and save a TelemetryProvider
  private TelemetryProvider createTelemetryProvider(
      UUID uuid, String name, Map<String, String> tags, TelemetryProviderConfig config) {
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setUuid(uuid);
    telemetryProvider.setCustomerUUID(customer.getUuid());
    telemetryProvider.setName(name);
    telemetryProvider.setTags(tags);
    telemetryProvider.setConfig(config);
    mockTelemetryProviderService.save(telemetryProvider);

    // Mock getOrBadRequest to return this provider when requested by UUID
    when(mockTelemetryProviderService.getOrBadRequest(uuid)).thenReturn(telemetryProvider);

    return telemetryProvider;
  }

  // Helper method to create AuditLogConfig with YSQL configuration
  private AuditLogConfig createAuditLogConfigWithYSQL(
      UUID exporterUuid, Map<String, String> additionalTags) {
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YSQLAuditConfig ysqlAuditConfig = new YSQLAuditConfig();
    ysqlAuditConfig.setEnabled(true);
    auditLogConfig.setYsqlAuditConfig(ysqlAuditConfig);

    UniverseLogsExporterConfig logsExporterConfig = new UniverseLogsExporterConfig();
    logsExporterConfig.setExporterUuid(exporterUuid);
    logsExporterConfig.setAdditionalTags(additionalTags);
    auditLogConfig.setUniverseLogsExporterConfig(ImmutableList.of(logsExporterConfig));

    return auditLogConfig;
  }

  // Helper method to create AuditLogConfig with YCQL configuration
  private AuditLogConfig createAuditLogConfigWithYCQL(
      UUID exporterUuid,
      Map<String, String> additionalTags,
      YCQLAuditConfig.YCQLAuditLogLevel logLevel) {
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YCQLAuditConfig ycqlAuditConfig = new YCQLAuditConfig();
    ycqlAuditConfig.setEnabled(true);
    ycqlAuditConfig.setLogLevel(logLevel);
    auditLogConfig.setYcqlAuditConfig(ycqlAuditConfig);

    UniverseLogsExporterConfig logsExporterConfig = new UniverseLogsExporterConfig();
    logsExporterConfig.setExporterUuid(exporterUuid);
    logsExporterConfig.setAdditionalTags(additionalTags);
    auditLogConfig.setUniverseLogsExporterConfig(ImmutableList.of(logsExporterConfig));

    return auditLogConfig;
  }

  // Helper method to create QueryLogConfig with YSQL configuration
  private QueryLogConfig createQueryLogConfig(
      UUID exporterUuid,
      Map<String, String> additionalTags,
      YSQLQueryLogConfig.YSQLLogStatement logStatement,
      YSQLQueryLogConfig.YSQlLogMinErrorStatement logMinErrorStatement,
      YSQLQueryLogConfig.YSQLLogErrorVerbosity logErrorVerbosity,
      boolean logDuration,
      boolean debugPrintPlan,
      boolean logConnections,
      boolean logDisconnections,
      int logMinDurationStatement) {
    QueryLogConfig queryLogConfig = new QueryLogConfig();
    YSQLQueryLogConfig ysqlQueryLogConfig = new YSQLQueryLogConfig();
    ysqlQueryLogConfig.setEnabled(true);
    ysqlQueryLogConfig.setLogStatement(logStatement);
    ysqlQueryLogConfig.setLogMinErrorStatement(logMinErrorStatement);
    ysqlQueryLogConfig.setLogErrorVerbosity(logErrorVerbosity);
    ysqlQueryLogConfig.setLogDuration(logDuration);
    ysqlQueryLogConfig.setDebugPrintPlan(debugPrintPlan);
    ysqlQueryLogConfig.setLogConnections(logConnections);
    ysqlQueryLogConfig.setLogDisconnections(logDisconnections);
    ysqlQueryLogConfig.setLogMinDurationStatement(logMinDurationStatement);
    queryLogConfig.setYsqlQueryLogConfig(ysqlQueryLogConfig);

    UniverseQueryLogsExporterConfig logsExporterConfig = new UniverseQueryLogsExporterConfig();
    logsExporterConfig.setExporterUuid(exporterUuid);
    logsExporterConfig.setAdditionalTags(additionalTags);
    queryLogConfig.setUniverseLogsExporterConfig(ImmutableList.of(logsExporterConfig));

    return queryLogConfig;
  }

  // Helper method to create MetricsExportConfig
  private MetricsExportConfig createMetricsExportConfig(
      UUID exporterUuid,
      Map<String, String> additionalTags,
      int scrapeIntervalSeconds,
      int scrapeTimeoutSeconds,
      MetricCollectionLevel collectionLevel) {
    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setScrapeIntervalSeconds(scrapeIntervalSeconds);
    metricsExportConfig.setScrapeTimeoutSeconds(scrapeTimeoutSeconds);
    metricsExportConfig.setCollectionLevel(collectionLevel);

    UniverseMetricsExporterConfig metricsExporterConfig = new UniverseMetricsExporterConfig();
    metricsExporterConfig.setExporterUuid(exporterUuid);
    metricsExporterConfig.setAdditionalTags(additionalTags);
    metricsExportConfig.setUniverseMetricsExporterConfig(ImmutableList.of(metricsExporterConfig));

    return metricsExportConfig;
  }

  // Helper method to generate config file and assert result
  private void generateAndAssertConfig(
      AuditLogConfig auditLogConfig,
      QueryLogConfig queryLogConfig,
      MetricsExportConfig metricsExportConfig,
      String expectedResource) {
    try {
      File file = new File(OTEL_COL_TMP_PATH + "config.yml");
      file.createNewFile();
      generator.generateConfigFile(
          nodeTaskParams,
          provider,
          null,
          auditLogConfig,
          queryLogConfig,
          metricsExportConfig,
          "%t | %u%d : ",
          file.toPath(),
          NodeManager.getOtelColMetricsPort(nodeTaskParams),
          null);

      String result = FileUtils.readFileToString(file, Charset.defaultCharset());
      String expected = TestUtils.readResource(expectedResource);
      assertThat(result, equalTo(expected));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void generateOtelColConfigYsqlPlusLoki() {
    LokiConfig config = new LokiConfig();
    config.setType(ProviderType.LOKI);
    config.setEndpoint("http://loki:3100");
    config.setAuthType(AuthType.NoAuth);

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "Loki", ImmutableMap.of("tag", "value"), config);

    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            telemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "otherValue"));

    generateAndAssertConfig(auditLogConfig, null, null, "audit/loki_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusLoki() {
    LokiConfig config = new LokiConfig();
    config.setType(ProviderType.LOKI);
    config.setEndpoint("http://loki:3100");
    config.setAuthType(AuthType.NoAuth);

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "Loki", ImmutableMap.of("tag", "value"), config);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "otherValue"),
            YSQLQueryLogConfig.YSQLLogStatement.ALL,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.VERBOSE,
            true,
            true,
            true,
            true,
            1000);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/loki_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlPlusOTLP() {
    OTLPConfig config = new OTLPConfig();
    config.setType(ProviderType.OTLP);
    config.setEndpoint("http://otlp:3100");
    config.setAuthType(AuthType.BasicAuth);

    AuthCredentials.BasicAuthCredentials authCredentials =
        new AuthCredentials.BasicAuthCredentials();
    authCredentials.setUsername("username");
    authCredentials.setPassword("password");
    config.setBasicAuth(authCredentials);

    Map<String, String> headers = new HashMap<>();
    headers.put("header1", "value1");
    headers.put("header2", "value2");
    config.setHeaders(headers);

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "OTLP", ImmutableMap.of("tag", "value"), config);

    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            telemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "otherValue"));

    generateAndAssertConfig(auditLogConfig, null, null, "audit/otlp_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusOTLP() {
    OTLPConfig config = new OTLPConfig();
    config.setType(ProviderType.OTLP);
    config.setEndpoint("http://otlp:3100");
    config.setAuthType(AuthType.NoAuth);
    config.setTimeoutSeconds(10);

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "OTLP", ImmutableMap.of("tag", "value"), config);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "otherValue"),
            YSQLQueryLogConfig.YSQLLogStatement.ALL,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.VERBOSE,
            true,
            true,
            true,
            true,
            1000);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/otlp_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlPlusDatadog() {
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("ddsite");
    config.setApiKey("apikey");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "DD", ImmutableMap.of("tag", "value"), config);

    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            telemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "otherValue"));

    generateAndAssertConfig(auditLogConfig, null, null, "audit/dd_config.yml");
  }

  @Test
  public void generateOtelColConfigAuditAndQueryLogPlusAWS() {
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");

    TelemetryProvider awsTelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "AWS", ImmutableMap.of("tag", "value"), awsConfig);

    // Create audit log config
    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            awsTelemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "auditValue"));

    // Create query log config
    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            awsTelemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "queryValue"),
            YSQLQueryLogConfig.YSQLLogStatement.MOD,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.TERSE,
            true,
            false,
            true,
            false,
            500);

    generateAndAssertConfig(
        auditLogConfig, queryLogConfig, null, "audit/aws_audit_and_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigAuditAndQueryLogPlusS3() {
    S3Config s3Config = new S3Config();
    s3Config.setType(ProviderType.S3);
    s3Config.setBucket("bucket");
    s3Config.setAccessKey("access_key");
    s3Config.setSecretKey("secret_key");
    s3Config.setRegion("us-west2");

    TelemetryProvider s3TelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "S3", ImmutableMap.of("tag", "value"), s3Config);

    // Create audit log config
    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            s3TelemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "auditValue"));

    // Create query log config
    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            s3TelemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "queryValue"),
            YSQLQueryLogConfig.YSQLLogStatement.MOD,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.TERSE,
            true,
            false,
            true,
            false,
            500);

    generateAndAssertConfig(
        auditLogConfig, queryLogConfig, null, "audit/s3_audit_and_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigAuditLogPlusS3WithUniverseNodePrefix() {
    S3Config s3Config = new S3Config();
    s3Config.setType(ProviderType.S3);
    s3Config.setBucket("bucket");
    s3Config.setAccessKey("access_key");
    s3Config.setSecretKey("secret_key");
    s3Config.setRegion("us-west2");
    s3Config.setIncludeUniverseAndNodeInPrefix(true);

    TelemetryProvider s3TelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "S3", ImmutableMap.of("tag", "value"), s3Config);

    // Create audit log config
    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            s3TelemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "auditValue"));

    generateAndAssertConfig(
        auditLogConfig, null, null, "audit/s3_audit_with_universe_node_prefix_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusDatadog() {
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("ddsite");
    config.setApiKey("apikey");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "DD", ImmutableMap.of("tag", "value"), config);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "otherValue"),
            YSQLQueryLogConfig.YSQLLogStatement.DDL,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.DEFAULT,
            false,
            false,
            false,
            false,
            -1);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/dd_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigYcqlPlusSplunk() {
    SplunkConfig config = new SplunkConfig();
    config.setType(ProviderType.SPLUNK);
    config.setEndpoint("endpoint");
    config.setIndex("index");
    config.setSource("source");
    config.setToken("apitoken");
    config.setSourceType("some_type");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "Splunk", ImmutableMap.of("tag", "value"), config);

    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYCQL(
            telemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "otherValue"),
            YCQLAuditConfig.YCQLAuditLogLevel.WARNING);

    generateAndAssertConfig(auditLogConfig, null, null, "audit/splunk_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusSplunk() {
    SplunkConfig config = new SplunkConfig();
    config.setType(ProviderType.SPLUNK);
    config.setEndpoint("endpoint");
    config.setIndex("index");
    config.setSource("source");
    config.setToken("apitoken");
    config.setSourceType("some_type");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "Splunk", ImmutableMap.of("tag", "value"), config);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "otherValue"),
            YSQLQueryLogConfig.YSQLLogStatement.MOD,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.TERSE,
            true,
            false,
            true,
            false,
            500);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/splunk_query_log_config.yml");
  }

  @Test
  public void generateMultiConfig() {
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");

    TelemetryProvider awsTelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "AWS", ImmutableMap.of("tag", "value"), awsConfig);

    GCPCloudMonitoringConfig gcpConfig = new GCPCloudMonitoringConfig();
    gcpConfig.setType(ProviderType.GCP_CLOUD_MONITORING);
    gcpConfig.setProject("project");
    gcpConfig.setCredentials(Json.parse("{\"creds\": \"some_creds\"}"));

    TelemetryProvider gcpTelemetryProvider =
        createTelemetryProvider(
            UUID.fromString("11111111-1111-1111-1111-111111111111"),
            "GCP",
            ImmutableMap.of("tag", "value1"),
            gcpConfig);

    // Create audit log config with both YCQL and YSQL, and multiple exporters
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YCQLAuditConfig ycqlAuditConfig = new YCQLAuditConfig();
    ycqlAuditConfig.setEnabled(true);
    ycqlAuditConfig.setLogLevel(YCQLAuditConfig.YCQLAuditLogLevel.WARNING);
    auditLogConfig.setYcqlAuditConfig(ycqlAuditConfig);

    YSQLAuditConfig ysqlAuditConfig = new YSQLAuditConfig();
    ysqlAuditConfig.setEnabled(true);
    auditLogConfig.setYsqlAuditConfig(ysqlAuditConfig);

    UniverseLogsExporterConfig logsExporterConfigAws = new UniverseLogsExporterConfig();
    logsExporterConfigAws.setExporterUuid(awsTelemetryProvider.getUuid());
    logsExporterConfigAws.setAdditionalTags(ImmutableMap.of("additionalTag", "otherValue"));

    UniverseLogsExporterConfig logsExporterConfigGcp = new UniverseLogsExporterConfig();
    logsExporterConfigGcp.setExporterUuid(gcpTelemetryProvider.getUuid());
    logsExporterConfigGcp.setAdditionalTags(ImmutableMap.of("additionalTag", "yetAnotherValue"));

    auditLogConfig.setUniverseLogsExporterConfig(
        ImmutableList.of(logsExporterConfigAws, logsExporterConfigGcp));

    generateAndAssertConfig(auditLogConfig, null, null, "audit/multi_config.yml");
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusAWS() {
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");

    TelemetryProvider awsTelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "AWS", ImmutableMap.of("tag", "value"), awsConfig);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            awsTelemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "queryValue"),
            YSQLQueryLogConfig.YSQLLogStatement.ALL,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.VERBOSE,
            true,
            true,
            true,
            true,
            100);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/aws_query_log_config.yml");
  }

  @Test
  public void generateOtelHelmValuesYsqlPlusAWS() {
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");

    TelemetryProvider awsTelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "AWS", ImmutableMap.of("tag", "value"), awsConfig);

    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(awsTelemetryProvider.getUuid(), ImmutableMap.of());

    String logLinePrefix = "%m [%p] ";

    try {
      Map<String, Object> overrides = generator.getOtelHelmValues(auditLogConfig, logLinePrefix);
      Yaml yaml = new Yaml(new SkipNullRepresenter());
      File file = new File(OTEL_COL_TMP_PATH + "k8_config.yml");
      file.createNewFile();
      try (BufferedWriter bw = new BufferedWriter(new FileWriter(file)); ) {
        yaml.dump(overrides, bw);
      }
      String result = FileUtils.readFileToString(file, Charset.defaultCharset());

      String expected = TestUtils.readResource("audit/k8s_helm_values.yml");
      assertThat(result, equalTo(expected));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void generateOtelColConfigYsqlQueryLogPlusGCP() {
    GCPCloudMonitoringConfig gcpConfig = new GCPCloudMonitoringConfig();
    gcpConfig.setType(ProviderType.GCP_CLOUD_MONITORING);
    gcpConfig.setProject("project");
    gcpConfig.setCredentials(Json.parse("{\"creds\": \"some_creds\"}"));

    TelemetryProvider gcpTelemetryProvider =
        createTelemetryProvider(
            UUID.fromString("11111111-1111-1111-1111-111111111111"),
            "GCP",
            ImmutableMap.of("tag", "value1"),
            gcpConfig);

    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            gcpTelemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "gcpValue"),
            YSQLQueryLogConfig.YSQLLogStatement.NONE,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.DEFAULT,
            false,
            false,
            false,
            false,
            -1);

    generateAndAssertConfig(null, queryLogConfig, null, "audit/gcp_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigMetricsPlusDataDog() {
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("ddsite");
    config.setApiKey("apikey");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "DataDog", ImmutableMap.of("tag", "value"), config);

    // Mock the getOrBadRequest method to return our telemetry provider
    when(mockTelemetryProviderService.getOrBadRequest(telemetryProvider.getUuid()))
        .thenReturn(telemetryProvider);

    MetricsExportConfig metricsExportConfig =
        createMetricsExportConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("env", "prod", "region", "us-west"),
            15,
            10,
            MetricCollectionLevel.NORMAL);

    // Set additional metrics-specific properties
    UniverseMetricsExporterConfig metricsExporterConfig =
        (UniverseMetricsExporterConfig)
            metricsExportConfig.getUniverseMetricsExporterConfig().get(0);
    metricsExporterConfig.setSendBatchSize(500);
    metricsExporterConfig.setSendBatchMaxSize(1000);
    metricsExporterConfig.setSendBatchTimeoutSeconds(5);
    metricsExporterConfig.setMetricsPrefix("");

    generateAndAssertConfig(null, null, metricsExportConfig, "audit/metrics_datadog_config.yml");
  }

  @Test
  public void generateOtelColConfigMetricsPlusOTLP() {
    OTLPConfig config = new OTLPConfig();
    config.setType(ProviderType.OTLP);
    config.setEndpoint("http://otlp:3100");
    config.setAuthType(AuthType.NoAuth);
    config.setLogsEndpoint("http://otlp:3000/logs");
    config.setMetricsEndpoint("http://otlp:3000/metrics");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "OTLP", ImmutableMap.of("tag", "value"), config);

    when(mockTelemetryProviderService.getOrBadRequest(telemetryProvider.getUuid()))
        .thenReturn(telemetryProvider);

    MetricsExportConfig metricsExportConfig =
        createMetricsExportConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("env", "prod", "region", "us-west"),
            15,
            10,
            MetricCollectionLevel.NORMAL);

    UniverseMetricsExporterConfig metricsExporterConfig =
        (UniverseMetricsExporterConfig)
            metricsExportConfig.getUniverseMetricsExporterConfig().get(0);
    metricsExporterConfig.setSendBatchSize(500);
    metricsExporterConfig.setSendBatchMaxSize(1000);
    metricsExporterConfig.setSendBatchTimeoutSeconds(5);
    metricsExporterConfig.setMetricsPrefix("");

    generateAndAssertConfig(null, null, metricsExportConfig, "audit/metrics_otlp_config.yml");
  }

  @Test
  public void generateOtelColConfigAuditAndQueryLogPlusMultiProvider() {
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");

    TelemetryProvider awsTelemetryProvider =
        createTelemetryProvider(new UUID(0, 0), "AWS", ImmutableMap.of("tag", "value"), awsConfig);

    GCPCloudMonitoringConfig gcpConfig = new GCPCloudMonitoringConfig();
    gcpConfig.setType(ProviderType.GCP_CLOUD_MONITORING);
    gcpConfig.setProject("project");
    gcpConfig.setCredentials(Json.parse("{\"creds\": \"some_creds\"}"));

    TelemetryProvider gcpTelemetryProvider =
        createTelemetryProvider(
            UUID.fromString("11111111-1111-1111-1111-111111111111"),
            "GCP",
            ImmutableMap.of("tag", "value1"),
            gcpConfig);

    // Create audit log config with AWS
    AuditLogConfig auditLogConfig =
        createAuditLogConfigWithYSQL(
            awsTelemetryProvider.getUuid(), ImmutableMap.of("additionalTag", "auditValue"));

    // Create query log config with GCP
    QueryLogConfig queryLogConfig =
        createQueryLogConfig(
            gcpTelemetryProvider.getUuid(),
            ImmutableMap.of("additionalTag", "queryValue"),
            YSQLQueryLogConfig.YSQLLogStatement.MOD,
            YSQLQueryLogConfig.YSQlLogMinErrorStatement.ERROR,
            YSQLQueryLogConfig.YSQLLogErrorVerbosity.TERSE,
            true,
            false,
            true,
            false,
            500);

    generateAndAssertConfig(
        auditLogConfig,
        queryLogConfig,
        null,
        "audit/multi_provider_audit_and_query_log_config.yml");
  }

  @Test
  public void generateOtelColConfigMetricsWithSpecificScrapeTargets() {
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("test");
    config.setApiKey("test");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(
            new UUID(0, 0), "Prometheus", ImmutableMap.of("tag", "value"), config);

    // Mock the getOrBadRequest method to return our telemetry provider
    when(mockTelemetryProviderService.getOrBadRequest(telemetryProvider.getUuid()))
        .thenReturn(telemetryProvider);

    MetricsExportConfig metricsExportConfig =
        createMetricsExportConfig(
            telemetryProvider.getUuid(),
            ImmutableMap.of("test", "specific_targets"),
            45,
            25,
            MetricCollectionLevel.ALL);

    // Only enable specific scrape targets
    metricsExportConfig.setScrapeConfigTargets(
        ImmutableSet.of(
            ScrapeConfigTargetType.MASTER_EXPORT,
            ScrapeConfigTargetType.TSERVER_EXPORT,
            ScrapeConfigTargetType.OTEL_EXPORT));

    generateAndAssertConfig(
        null, null, metricsExportConfig, "audit/metrics_specific_targets_config.yml");
  }

  @Test
  public void generateOtelColConfigMetricsWithMinimalCollectionLevel() {
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("test");
    config.setApiKey("test");

    TelemetryProvider telemetryProvider =
        createTelemetryProvider(
            new UUID(0, 0), "Minimal Metrics", ImmutableMap.of("tag", "value"), config);

    // Mock the getOrBadRequest method to return our telemetry provider
    when(mockTelemetryProviderService.getOrBadRequest(telemetryProvider.getUuid()))
        .thenReturn(telemetryProvider);

    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setCollectionLevel(MetricCollectionLevel.MINIMAL);

    UniverseMetricsExporterConfig metricsExporterConfig = new UniverseMetricsExporterConfig();
    metricsExporterConfig.setExporterUuid(telemetryProvider.getUuid());
    metricsExportConfig.setUniverseMetricsExporterConfig(ImmutableList.of(metricsExporterConfig));

    generateAndAssertConfig(
        null, null, metricsExportConfig, "audit/metrics_minimal_level_config.yml");
  }

  @Test
  public void generateOtelColConfigMetricsWithDynatrace() {
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setUuid(new UUID(0, 0));
    telemetryProvider.setCustomerUUID(customer.getUuid());
    telemetryProvider.setName("Dynatrace");
    telemetryProvider.setTags(ImmutableMap.of("tag", "value"));
    DynatraceConfig config = new DynatraceConfig();
    config.setType(ProviderType.DYNATRACE);
    config.setEndpoint("https://test.live.dynatrace.com");
    config.setApiToken("dynatrace-api-token");
    telemetryProvider.setConfig(config);
    mockTelemetryProviderService.save(telemetryProvider);

    // Mock the getOrBadRequest method to return our telemetry provider
    when(mockTelemetryProviderService.getOrBadRequest(telemetryProvider.getUuid()))
        .thenReturn(telemetryProvider);

    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setScrapeIntervalSeconds(15);
    metricsExportConfig.setScrapeTimeoutSeconds(10);
    metricsExportConfig.setCollectionLevel(MetricCollectionLevel.NORMAL);

    UniverseMetricsExporterConfig metricsExporterConfig = new UniverseMetricsExporterConfig();
    metricsExporterConfig.setExporterUuid(telemetryProvider.getUuid());
    metricsExporterConfig.setAdditionalTags(ImmutableMap.of("env", "prod", "region", "us-west"));
    metricsExporterConfig.setSendBatchSize(500);
    metricsExporterConfig.setSendBatchMaxSize(1000);
    metricsExporterConfig.setSendBatchTimeoutSeconds(5);
    metricsExporterConfig.setMetricsPrefix("ybdb.");

    metricsExportConfig.setUniverseMetricsExporterConfig(ImmutableList.of(metricsExporterConfig));

    generateAndAssertConfig(null, null, metricsExportConfig, "audit/dt_config.yml");
  }

  @Test
  public void testDynatraceEndpointTrailingSlashHandling() {
    // Test that trailing slashes are handled correctly
    DynatraceConfig config = new DynatraceConfig();
    config.setEndpoint("https://test.live.dynatrace.com/");

    // Should return endpoint without trailing slash
    assertThat(config.getCleanEndpoint(), equalTo("https://test.live.dynatrace.com"));

    // Test with no trailing slash
    config.setEndpoint("https://test.live.dynatrace.com");
    assertThat(config.getCleanEndpoint(), equalTo("https://test.live.dynatrace.com"));

    // Test with multiple trailing slashes
    config.setEndpoint("https://test.live.dynatrace.com///");
    assertThat(config.getCleanEndpoint(), equalTo("https://test.live.dynatrace.com"));

    // Test with null endpoint
    config.setEndpoint(null);
    assertThat(config.getCleanEndpoint(), equalTo(null));
  }
}
