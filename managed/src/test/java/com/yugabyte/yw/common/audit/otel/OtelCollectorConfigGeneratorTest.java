// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.audit.otel;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.telemetry.*;
import java.io.File;
import java.io.IOException;
import java.nio.charset.Charset;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class OtelCollectorConfigGeneratorTest extends FakeDBApplication {

  private static String OTEL_COL_TMP_PATH = "/tmp/otel/";

  private OtelCollectorConfigGenerator generator;
  private TelemetryProviderService telemetryProviderService;
  private Customer customer;
  private Provider provider;
  private Universe universe;
  private NodeTaskParams nodeTaskParams;

  @Before
  public void setUp() {
    new File(OTEL_COL_TMP_PATH).mkdir();
    generator = app.injector().instanceOf(OtelCollectorConfigGenerator.class);
    telemetryProviderService = app.injector().instanceOf(TelemetryProviderService.class);
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    universe = ModelFactory.createUniverse(customer.getId());
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    // update the node name
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams params = universe.getUniverseDetails();
                for (NodeDetails node : params.nodeDetailsSet) {
                  node.nodeName = "test-node";
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

  @Test
  public void generateOtelColConfigYsqlPlusDatadog() {
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setCustomerUUID(customer.getUuid());
    telemetryProvider.setName("DD");
    telemetryProvider.setTags(ImmutableMap.of("tag", "value"));
    DataDogConfig config = new DataDogConfig();
    config.setType(ProviderType.DATA_DOG);
    config.setSite("ddsite");
    config.setApiKey("apikey");
    telemetryProvider.setConfig(config);
    telemetryProviderService.save(telemetryProvider);

    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YSQLAuditConfig ysqlAuditConfig = new YSQLAuditConfig();
    ysqlAuditConfig.setEnabled(true);
    auditLogConfig.setYsqlAuditConfig(ysqlAuditConfig);
    UniverseLogsExporterConfig logsExporterConfig = new UniverseLogsExporterConfig();
    logsExporterConfig.setExporterUuid(telemetryProvider.getUuid());
    logsExporterConfig.setAdditionalTags(ImmutableMap.of("additionalTag", "otherValue"));
    auditLogConfig.setUniverseLogsExporterConfig(ImmutableList.of(logsExporterConfig));

    try {
      File file = new File(OTEL_COL_TMP_PATH + "config.yml");
      file.createNewFile();
      generator.generateConfigFile(
          nodeTaskParams,
          provider,
          null,
          auditLogConfig,
          "%t | %u%d : ",
          file.toPath(),
          NodeManager.getOtelColMetricsPort(nodeTaskParams));

      String result = FileUtils.readFileToString(file, Charset.defaultCharset());

      String expected = TestUtils.readResource("audit/dd_config.yml");
      assertThat(result, equalTo(expected));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void generateOtelColConfigYcqlPlusSplunk() {
    TelemetryProvider telemetryProvider = new TelemetryProvider();
    telemetryProvider.setCustomerUUID(customer.getUuid());
    telemetryProvider.setName("Splunk");
    telemetryProvider.setTags(ImmutableMap.of("tag", "value"));
    SplunkConfig config = new SplunkConfig();
    config.setType(ProviderType.SPLUNK);
    config.setEndpoint("endpoint");
    config.setIndex("index");
    config.setSource("source");
    config.setToken("apitoken");
    config.setSourceType("some_type");
    telemetryProvider.setConfig(config);
    telemetryProviderService.save(telemetryProvider);

    AuditLogConfig auditLogConfig = new AuditLogConfig();
    YCQLAuditConfig ycqlAuditConfig = new YCQLAuditConfig();
    ycqlAuditConfig.setEnabled(true);
    ycqlAuditConfig.setLogLevel(YCQLAuditConfig.YCQLAuditLogLevel.WARNING);
    auditLogConfig.setYcqlAuditConfig(ycqlAuditConfig);
    UniverseLogsExporterConfig logsExporterConfig = new UniverseLogsExporterConfig();
    logsExporterConfig.setExporterUuid(telemetryProvider.getUuid());
    logsExporterConfig.setAdditionalTags(ImmutableMap.of("additionalTag", "otherValue"));
    auditLogConfig.setUniverseLogsExporterConfig(ImmutableList.of(logsExporterConfig));

    try {
      File file = new File(OTEL_COL_TMP_PATH + "config.yml");
      file.createNewFile();
      generator.generateConfigFile(
          nodeTaskParams,
          provider,
          null,
          auditLogConfig,
          "%t | %u%d : ",
          file.toPath(),
          NodeManager.getOtelColMetricsPort(nodeTaskParams));

      String result = FileUtils.readFileToString(file, Charset.defaultCharset());

      String expected = TestUtils.readResource("audit/splunk_config.yml");
      assertThat(result, equalTo(expected));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void generateMultiConfig() {
    TelemetryProvider awsTelemetryProvider = new TelemetryProvider();
    awsTelemetryProvider.setCustomerUUID(customer.getUuid());
    awsTelemetryProvider.setName("AWS");
    awsTelemetryProvider.setTags(ImmutableMap.of("tag", "value"));
    AWSCloudWatchConfig awsConfig = new AWSCloudWatchConfig();
    awsConfig.setType(ProviderType.AWS_CLOUDWATCH);
    awsConfig.setEndpoint("endpoint");
    awsConfig.setAccessKey("access_key");
    awsConfig.setSecretKey("secret_key");
    awsConfig.setLogGroup("logGroup");
    awsConfig.setLogStream("logStream");
    awsConfig.setRegion("us-west2");
    awsTelemetryProvider.setConfig(awsConfig);
    telemetryProviderService.save(awsTelemetryProvider);

    TelemetryProvider gcpTelemetryProvider = new TelemetryProvider();
    gcpTelemetryProvider.setCustomerUUID(customer.getUuid());
    gcpTelemetryProvider.setName("GCP");
    gcpTelemetryProvider.setTags(ImmutableMap.of("tag", "value1"));
    GCPCloudMonitoringConfig gcpConfig = new GCPCloudMonitoringConfig();
    gcpConfig.setType(ProviderType.GCP_CLOUD_MONITORING);
    gcpConfig.setProject("project");
    gcpConfig.setCredentials(Json.parse("{\"creds\": \"some_creds\"}"));
    gcpTelemetryProvider.setConfig(gcpConfig);
    telemetryProviderService.save(gcpTelemetryProvider);

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

    try {
      File file = new File(OTEL_COL_TMP_PATH + "config.yml");
      file.createNewFile();
      generator.generateConfigFile(
          nodeTaskParams,
          provider,
          null,
          auditLogConfig,
          "%t | %u%d : ",
          file.toPath(),
          NodeManager.getOtelColMetricsPort(nodeTaskParams));

      String result = FileUtils.readFileToString(file, Charset.defaultCharset());

      String expected = TestUtils.readResource("audit/multi_config.yml");
      assertThat(result, equalTo(expected));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
