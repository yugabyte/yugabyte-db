// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.models.ExportTelemetryConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import java.util.Map;
import java.util.concurrent.CancellationException;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.YBClientApi;

public class SetFlagInMemoryTest extends CommissionerBaseTest {

  private static final String NODE_NAME = "k8s-n1";
  private static final String USER_PG_CONF_CSV = "work_mem=4MB,work_mem=64MB";

  private Universe universe;
  private YBClientApi mockClient;

  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClientApi.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    universe = createUniverse("k8s-universe", defaultCustomer.getId(), CloudType.kubernetes);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              NodeDetails node = new NodeDetails();
              node.nodeName = NODE_NAME;
              node.isTserver = true;
              node.isMaster = true;
              node.state = NodeDetails.NodeState.Live;
              node.tserverRpcPort = 9100;
              node.masterRpcPort = 7100;
              node.cloudInfo = new CloudSpecificInfo();
              node.cloudInfo.private_ip = "10.0.0.1";
              node.placementUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
              u.getUniverseDetails().nodeDetailsSet.add(node);
            });
  }

  private void runSetFlagInMemory(ServerType serverType) {
    runSetFlagInMemory(serverType, false /* ignoreErrors */);
  }

  private void runSetFlagInMemory(ServerType serverType, boolean ignoreErrors) {
    SetFlagInMemory.Params params = new SetFlagInMemory.Params();
    params.ignoreErrors = ignoreErrors;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = NODE_NAME;
    params.serverType = serverType;
    params.force = true;
    params.gflags = Map.of(GFlagsUtil.YSQL_PG_CONF_CSV, USER_PG_CONF_CSV);
    SetFlagInMemory task = app.injector().instanceOf(SetFlagInMemory.class);
    task.initialize(params);
    task.run();
  }

  private void persistTelemetryConfig(TelemetryConfig telemetryConfig) {
    ExportTelemetryConfig row = new ExportTelemetryConfig();
    row.setUniverseUuid(universe.getUniverseUUID());
    row.setTelemetryConfig(telemetryConfig);
    row.save();
  }

  private static TelemetryConfig ycqlOnlyAuditConfig() {
    YCQLAuditConfig ycqlAuditConfig = new YCQLAuditConfig();
    ycqlAuditConfig.setEnabled(true);
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    auditLogConfig.setYcqlAuditConfig(ycqlAuditConfig);
    return TelemetryConfig.builder().auditLogConfig(auditLogConfig).build();
  }

  @Test
  public void testK8sKeepsRawValueWithoutExportRow() throws Exception {
    runSetFlagInMemory(ServerType.TSERVER);
    // Helm passes the user value through unchanged when there is no export row.
    verify(mockClient)
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
  }

  @Test
  public void testK8sNormalizesValueLikeHelmWithAuditSection() throws Exception {
    // A YCQL-only audit config emits no ysql_pg_conf_csv entries, but helm still runs the value
    // through overrideWithTelemetryYsqlPgConfCsv, which keeps the first entry for a key.
    persistTelemetryConfig(ycqlOnlyAuditConfig());

    runSetFlagInMemory(ServerType.TSERVER);
    verify(mockClient)
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq("work_mem=4MB"), eq(true));
  }

  @Test
  public void testK8sMasterKeepsRawValueWithAuditSection() throws Exception {
    // Helm applies telemetry only to tservers.
    persistTelemetryConfig(ycqlOnlyAuditConfig());

    runSetFlagInMemory(ServerType.MASTER);
    verify(mockClient)
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
  }

  @Test
  public void testK8sKeepsRawValueWithMetricsOnlyRow() throws Exception {
    // A metrics-only row has no audit or query section, so helm keeps the raw value.
    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    persistTelemetryConfig(
        TelemetryConfig.builder().metricsExportConfig(metricsExportConfig).build());

    runSetFlagInMemory(ServerType.TSERVER);
    verify(mockClient)
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
  }

  @Test
  public void testFailedRpcFailsTask() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(false);
    assertThrows(RuntimeException.class, () -> runSetFlagInMemory(ServerType.TSERVER));
  }

  @Test
  public void testIgnoreErrorsSkipsFailedRpc() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new RuntimeException("rpc failed"));
    runSetFlagInMemory(ServerType.TSERVER, true /* ignoreErrors */);
    verify(mockClient)
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
  }

  @Test
  public void testIgnoreErrorsRethrowsCancellation() throws Exception {
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new RuntimeException(new CancellationException("aborted")));
    assertThrows(
        RuntimeException.class,
        () -> runSetFlagInMemory(ServerType.TSERVER, true /* ignoreErrors */));
  }

  @Test
  public void testIgnoreErrorsRethrowsAbort() throws Exception {
    // An abort interrupts the RPC; ignoreErrors must not turn it into success.
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenThrow(new RuntimeException(new InterruptedException()));
    assertThrows(
        RuntimeException.class,
        () -> runSetFlagInMemory(ServerType.TSERVER, true /* ignoreErrors */));
  }
}
