// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anySet;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.lang.reflect.Method;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonNet.HostPortPB;
import org.yb.cdc.CdcConsumer.ConsumerRegistryPB;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

public class XClusterSchedulerTest extends FakeDBApplication {

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private XClusterConfig xClusterConfig;
  private YBClient mockClient;
  private XClusterScheduler scheduler;

  @Before
  public void setUp() throws Exception {
    Customer defaultCustomer = testCustomer("XClusterScheduler-test-customer");

    UUID sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse("source-universe", sourceUniverseUUID);
    UniverseDefinitionTaskParams sourceDetails = sourceUniverse.getUniverseDetails();
    NodeDetails sourceMaster = new NodeDetails();
    sourceMaster.isMaster = true;
    sourceMaster.isTserver = true;
    sourceMaster.state = NodeState.Live;
    sourceMaster.cloudInfo = new CloudSpecificInfo();
    sourceMaster.cloudInfo.private_ip = "10.0.0.1";
    sourceMaster.masterRpcPort = 7100;
    sourceMaster.placementUuid = sourceUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    sourceDetails.nodeDetailsSet.add(sourceMaster);
    sourceUniverse.setUniverseDetails(sourceDetails);
    sourceUniverse.update();

    targetUniverse = createUniverse("target-universe", UUID.randomUUID());

    XClusterConfigCreateFormData createFormData = new XClusterConfigCreateFormData();
    createFormData.name = "test-config";
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    createFormData.tables = Collections.singleton("exampleTableId");
    xClusterConfig = XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    mockClient = mock(YBClient.class);
    when(mockService.getUniverseClient(any())).thenReturn(mockClient);

    // Construct XClusterScheduler directly so we control its YBClientService dependency
    // without inheriting CommissionerBaseTest's many unrelated mockBaseTaskDependencies stubs.
    RuntimeConfGetter confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    scheduler =
        new XClusterScheduler(
            mock(PlatformScheduler.class),
            confGetter,
            mockService,
            mock(XClusterUniverseService.class),
            mock(MetricService.class),
            mock(UniverseTableHandler.class));
  }

  /**
   * Verifies that syncMasterAddressesForConfig calls alterUniverseReplicationSourceMasterAddresses
   * when the current master addresses in the replication group differ from the source universe's
   * current master addresses.
   */
  @Test
  public void testSyncMasterAddressesCallsAlterWhenAddressesDiffer() throws Exception {
    // Stale address stored in the target universe's replication group.
    HostPortPB staleAddress = HostPortPB.newBuilder().setHost("9.9.9.9").setPort(7100).build();
    ProducerEntryPB producerEntry =
        ProducerEntryPB.newBuilder().addMasterAddrs(staleAddress).build();
    String replicationGroupName = xClusterConfig.getReplicationGroupName();
    ConsumerRegistryPB consumerRegistry =
        ConsumerRegistryPB.newBuilder().putProducerMap(replicationGroupName, producerEntry).build();
    CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(consumerRegistry)
            .build();
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(0, "", clusterConfig, null);
    when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);

    // The alter call succeeds.
    AlterUniverseReplicationResponse mockAlterResponse =
        new AlterUniverseReplicationResponse(0, "", null);
    when(mockClient.alterUniverseReplicationSourceMasterAddresses(any(), anySet()))
        .thenReturn(mockAlterResponse);

    Method method =
        XClusterScheduler.class.getDeclaredMethod(
            "syncMasterAddressesForConfig", XClusterConfig.class);
    method.setAccessible(true);
    method.invoke(scheduler, xClusterConfig);

    // The stale address "9.9.9.9:7100" differs from the source universe master "10.0.0.1:7100",
    // so the scheduler must call alterUniverseReplicationSourceMasterAddresses.
    verify(mockClient)
        .alterUniverseReplicationSourceMasterAddresses(eq(replicationGroupName), anySet());
  }

  /**
   * Verifies that syncMasterAddressesForConfig skips the alter call when the master addresses in
   * the replication group already match the source universe's current master addresses.
   */
  @Test
  public void testSyncMasterAddressesSkipsAlterWhenAddressesMatch() throws Exception {
    // Current address already matches the source universe master "10.0.0.1:7100".
    HostPortPB currentAddress = HostPortPB.newBuilder().setHost("10.0.0.1").setPort(7100).build();
    ProducerEntryPB producerEntry =
        ProducerEntryPB.newBuilder().addMasterAddrs(currentAddress).build();
    String replicationGroupName = xClusterConfig.getReplicationGroupName();
    ConsumerRegistryPB consumerRegistry =
        ConsumerRegistryPB.newBuilder().putProducerMap(replicationGroupName, producerEntry).build();
    CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(consumerRegistry)
            .build();
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(0, "", clusterConfig, null);
    when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);

    Method method =
        XClusterScheduler.class.getDeclaredMethod(
            "syncMasterAddressesForConfig", XClusterConfig.class);
    method.setAccessible(true);
    method.invoke(scheduler, xClusterConfig);

    // No difference in master addresses, so alterUniverseReplicationSourceMasterAddresses must
    // NOT be called.
    verify(mockClient, never()).alterUniverseReplicationSourceMasterAddresses(any(), anySet());
  }
}
