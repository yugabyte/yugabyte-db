// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.payload;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.RETURNS_DEEP_STUBS;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageOtelCollector;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.FileHelperService;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigGenerator;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.nodeagent.InstallOtelCollectorInput;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class NodeAgentRpcPayloadTest extends FakeDBApplication {

  private NodeAgentClient mockNodeAgentClient;
  private OtelCollectorConfigGenerator mockConfigGenerator;
  private TelemetryProviderService mockTelemetryProviderService;
  private NodeAgentRpcPayload payload;
  private Universe universe;
  private NodeDetails node;
  private UserIntent userIntent;
  private NodeAgent nodeAgent;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "us-west-2", "US West 2", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    InstanceType instanceType =
        InstanceType.upsert(
            provider.getUuid(), "c5.large", 2, 4.0, new InstanceType.InstanceTypeDetails());
    userIntent = ApiUtils.getTestUserIntent(region, provider, instanceType, 3);
    universe = ModelFactory.createUniverse("otel-payload", customer.getId());
    universe =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    // The package file name depends on the universe architecture.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.arch = Architecture.x86_64;
              u.setUniverseDetails(details);
            });
    node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    nodeAgent = new NodeAgent();
    nodeAgent.setVersion(Util.getYbaVersion());

    mockNodeAgentClient = mock(NodeAgentClient.class);
    mockConfigGenerator = mock(OtelCollectorConfigGenerator.class);
    mockTelemetryProviderService = mock(TelemetryProviderService.class);
    when(mockConfigGenerator.generateAuditLineStartEre(any())).thenReturn("");
    when(mockConfigGenerator.generateConfigFile(any(), any(), any(), any(), any(), anyInt(), any()))
        .thenReturn(Paths.get("/tmp/otel-collector-config.yml"));
    payload =
        new NodeAgentRpcPayload(
            mock(ReleaseManager.class),
            mock(Config.class),
            app.injector().instanceOf(RuntimeConfGetter.class),
            mockConfigGenerator,
            mockTelemetryProviderService,
            mock(FileHelperService.class),
            mockNodeAgentClient,
            mock(NodeUniverseManager.class),
            mock(NodeManager.class));
  }

  private ManageOtelCollector.Params params(
      boolean otelCollectorEnabled, TelemetryConfig telemetryConfig) {
    ManageOtelCollector.Params params = new ManageOtelCollector.Params();
    params.nodeName = node.nodeName;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.azUuid = node.azUuid;
    params.otelCollectorEnabled = otelCollectorEnabled;
    params.telemetryConfig = telemetryConfig;
    params.deviceInfo = userIntent.deviceInfo;
    params.gflags = new HashMap<>();
    return params;
  }

  private TelemetryConfig metricsExportTelemetryConfig() {
    UUID exporterUuid = UUID.randomUUID();
    TelemetryProvider telemetryProvider = mock(TelemetryProvider.class, RETURNS_DEEP_STUBS);
    when(telemetryProvider.getConfig().getType()).thenReturn(ProviderType.DATA_DOG);
    when(mockTelemetryProviderService.get(exporterUuid)).thenReturn(telemetryProvider);
    UniverseMetricsExporterConfig exporterConfig = new UniverseMetricsExporterConfig();
    exporterConfig.setExporterUuid(exporterUuid);
    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setUniverseMetricsExporterConfig(List.of(exporterConfig));
    TelemetryConfig telemetryConfig = new TelemetryConfig();
    telemetryConfig.setMetricsExportConfig(metricsExportConfig);
    return telemetryConfig;
  }

  @Test
  public void testDisableAllExportStopsCollectorWithoutPackage() {
    InstallOtelCollectorInput input =
        payload.setupInstallOtelCollectorBits(
            universe, node, params(true, new TelemetryConfig()), nodeAgent);

    assertTrue(input.getDisableCollector());
    assertFalse(input.getRefreshScriptOnly());
    assertEquals("", input.getOtelColPackagePath());
    assertEquals("", input.getOtelColConfigFile());
    verify(mockNodeAgentClient, never()).uploadFile(any(), any(), any(), any(), anyInt(), any());
  }

  @Test
  public void testDisableAllExportOnOlderNodeAgentUsesFullInstall() {
    nodeAgent.setVersion("2.20.0.0-b1");
    InstallOtelCollectorInput input =
        payload.setupInstallOtelCollectorBits(
            universe, node, params(true, new TelemetryConfig()), nodeAgent);

    assertFalse(input.getDisableCollector());
    assertFalse(input.getRefreshScriptOnly());
    assertFalse(input.getOtelColPackagePath().isEmpty());
    // The full install stops the collector and does not start it again without a config file.
    assertEquals("", input.getOtelColConfigFile());
    verify(mockNodeAgentClient, times(1))
        .uploadFile(eq(nodeAgent), any(), anyString(), any(), anyInt(), any());
  }

  @Test
  public void testDisableAllExportWithFastDisableOffUsesFullInstall() {
    RuntimeConfigEntry.upsert(
        universe, UniverseConfKeys.otelCollectorFastDisable.getKey(), "false");
    InstallOtelCollectorInput input =
        payload.setupInstallOtelCollectorBits(
            universe, node, params(true, new TelemetryConfig()), nodeAgent);

    assertFalse(input.getDisableCollector());
    assertFalse(input.getOtelColPackagePath().isEmpty());
    verify(mockNodeAgentClient, times(1))
        .uploadFile(eq(nodeAgent), any(), anyString(), any(), anyInt(), any());
  }

  @Test
  public void testCollectorNotInstalledUsesRefreshOnly() {
    InstallOtelCollectorInput input =
        payload.setupInstallOtelCollectorBits(
            universe, node, params(false, new TelemetryConfig()), nodeAgent);

    assertTrue(input.getRefreshScriptOnly());
    assertFalse(input.getDisableCollector());
    verify(mockNodeAgentClient, never()).uploadFile(any(), any(), any(), any(), anyInt(), any());
  }

  @Test
  public void testEnabledExportInstallsPackageAndConfig() {
    InstallOtelCollectorInput input =
        payload.setupInstallOtelCollectorBits(
            universe, node, params(true, metricsExportTelemetryConfig()), nodeAgent);

    assertFalse(input.getDisableCollector());
    assertFalse(input.getRefreshScriptOnly());
    assertFalse(input.getOtelColPackagePath().isEmpty());
    assertTrue(input.getOtelColConfigFile().endsWith("otel-collector-config.yml"));
    // One upload for the package, one for the config file.
    verify(mockNodeAgentClient, times(2))
        .uploadFile(eq(nodeAgent), any(), anyString(), any(), anyInt(), any());
  }
}
