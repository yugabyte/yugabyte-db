// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YbcClient;
import org.yb.ybc.ControllerStatus;
import org.yb.ybc.RpcControllerStatus;
import org.yb.ybc.UpgradeResponse;
import org.yb.ybc.UpgradeResultResponse;

@RunWith(MockitoJUnitRunner.class)
public class YbcUpgradeTest extends FakeDBApplication {

  @Mock PlatformScheduler mockPlatformScheduler;
  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;
  @Mock Config mockAppConfig;

  MockedStatic<Util> mockedUtil;

  private YbcClient mockYbcClient;
  private YbcClient mockYbcClient2;
  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private YbcUpgrade ybcUpgrade;

  private final String NEW_YBC_VERSION = "2.0.0-b1";

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_NODE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(1);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(1);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockAppConfig);
    when(mockYbcManager.getStableYbcVersion()).thenReturn(NEW_YBC_VERSION);
    mockYbcClient = mock(YbcClient.class);
    mockYbcClient2 = mock(YbcClient.class);
    ybcUpgrade =
        new YbcUpgrade(
            mockPlatformScheduler, mockRuntimeConfigFactory, mockYbcClientService, mockYbcManager);

    mockedUtil = Mockito.mockStatic(Util.class);
    mockedUtil.when(() -> Util.getNodeHomeDir(any(), any())).thenReturn("/home/yugabyte");
  }

  @After
  public void TearDown() {
    mockedUtil.close();
  }

  @Test
  public void testUpgradeSuccess() {
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    // assertEquals(false, true);
  }

  @Test
  public void testUpgradeRequestFailure() {
    when(mockYbcClient.Upgrade(any())).thenReturn(null);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().ybcSoftwareVersion;
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
  }

  @Test
  public void testIgnoreFewUpgradeWithUniverseBatchSize() {
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-2",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    String oldYbcVersion = universe.getUniverseDetails().ybcSoftwareVersion;
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    new YbcUpgrade(
            mockPlatformScheduler, mockRuntimeConfigFactory, mockYbcClientService, mockYbcManager)
        .scheduleRunner();
    Set<String> universeYbcVersions = new HashSet<>();
    universeYbcVersions.add(
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    universeYbcVersions.add(
        Universe.getOrBadRequest(universe.universeUUID).getUniverseDetails().ybcSoftwareVersion);
    Set<String> expectedUniversesYbcVersions = new HashSet<>();
    expectedUniversesYbcVersions.add(oldYbcVersion);
    expectedUniversesYbcVersions.add(NEW_YBC_VERSION);
    assertEquals(expectedUniversesYbcVersions, universeYbcVersions);
  }

  @Test
  public void testIgnoreFewUpgradeWithNodeBatchSize() {
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-2",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    String oldYbcVersion = universe.getUniverseDetails().ybcSoftwareVersion;
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_NODE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(1);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(2);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockAppConfig);
    new YbcUpgrade(
            mockPlatformScheduler, mockRuntimeConfigFactory, mockYbcClientService, mockYbcManager)
        .scheduleRunner();
    Set<String> universeYbcVersions = new HashSet<>();
    universeYbcVersions.add(
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    universeYbcVersions.add(
        Universe.getOrBadRequest(universe.universeUUID).getUniverseDetails().ybcSoftwareVersion);
    Set<String> expectedUniversesYbcVersions = new HashSet<>();
    expectedUniversesYbcVersions.add(oldYbcVersion);
    expectedUniversesYbcVersions.add(NEW_YBC_VERSION);
    assertEquals(expectedUniversesYbcVersions, universeYbcVersions);
  }

  @Test
  public void testUpgradeAllWithUniverseBatchSize() {
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-3",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_NODE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(4);
    when(mockAppConfig.getInt(YbcUpgrade.YBC_UNIVERSE_UPGRADE_BATCH_SIZE_PATH)).thenReturn(2);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockAppConfig);
    new YbcUpgrade(
            mockPlatformScheduler, mockRuntimeConfigFactory, mockYbcClientService, mockYbcManager)
        .scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(universe.universeUUID).getUniverseDetails().ybcSoftwareVersion);
  }

  @Test
  public void testUpgradeFailedUniverse() {
    when(mockYbcClient.Upgrade(any())).thenReturn(null);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().ybcSoftwareVersion;
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
  }

  @Test
  public void testUpgradeRequestOnUnreachableNodes() {
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder().setStatus(ControllerStatus.COMPLETE).build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    List<NodeDetails> nodes = new ArrayList<>(defaultUniverse.getNodes());
    when(mockYbcClientService.getNewClient(
            nodes.get(0).cloudInfo.private_ip,
            defaultUniverse.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
            defaultUniverse.getCertificateNodetoNode()))
        .thenReturn(mockYbcClient);
    when(mockYbcClient2.Upgrade(any())).thenReturn(null);
    when(mockYbcClientService.getNewClient(
            nodes.get(1).cloudInfo.private_ip,
            defaultUniverse.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
            defaultUniverse.getCertificateNodetoNode()))
        .thenReturn(mockYbcClient2);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().ybcSoftwareVersion;
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
    when(mockYbcClient2.Upgrade(any())).thenReturn(resp);
    when(mockYbcClient2.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(
            nodes.get(1).cloudInfo.private_ip,
            defaultUniverse.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
            defaultUniverse.getCertificateNodetoNode()))
        .thenReturn(mockYbcClient2);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.universeUUID)
            .getUniverseDetails()
            .ybcSoftwareVersion);
  }
}
