// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
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

@RunWith(MockitoJUnitRunner.Silent.class)
public class YbcUpgradeTest extends FakeDBApplication {

  @Mock PlatformScheduler mockPlatformScheduler;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock Config mockAppConfig;
  @Mock NodeUniverseManager mockNodeUniverseManager;
  @Mock NodeManager mockNodeManager;
  @Mock ReleaseManager mockReleaseManager;
  @Mock KubernetesManagerFactory mockKubernetesManagerFactory;

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
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcNodeBatchSize))).thenReturn(1);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcUniverseBatchSize))).thenReturn(1);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.maxYbcUpgradePollResultTries)).thenReturn(2);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.ybcUpgradePollResultSleepMs)).thenReturn(10L);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybcAllowScheduledUpgrade)))
        .thenReturn(true);
    when(mockYbcManager.getStableYbcVersion()).thenReturn(NEW_YBC_VERSION);

    ShellResponse dummyShellUploadResponse = ShellResponse.create(0, "");
    lenient()
        .when(mockNodeUniverseManager.runCommand(any(), any(), anyList(), any()))
        .thenReturn(dummyShellUploadResponse);
    lenient().when(mockYbcManager.getYbcPackageTmpLocation(any(), any(), any())).thenReturn("/tmp");
    mockYbcClient = mock(YbcClient.class);
    mockYbcClient2 = mock(YbcClient.class);
    ybcUpgrade =
        new YbcUpgrade(
            mockAppConfig,
            mockPlatformScheduler,
            mockConfGetter,
            mockYbcClientService,
            mockYbcManager,
            mockNodeUniverseManager,
            mockNodeManager);

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
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeRequestFailure() {
    when(mockYbcClient.Upgrade(any())).thenReturn(null);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }

  @Test
  public void testIgnoreFewUpgradeWithUniverseBatchSize() {
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-2",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    String oldYbcVersion = universe.getUniverseDetails().getYbcSoftwareVersion();
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    new YbcUpgrade(
            mockAppConfig,
            mockPlatformScheduler,
            mockConfGetter,
            mockYbcClientService,
            mockYbcManager,
            mockNodeUniverseManager,
            mockNodeManager)
        .scheduleRunner();
    Set<String> universeYbcVersions = new HashSet<>();
    universeYbcVersions.add(
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    universeYbcVersions.add(
        Universe.getOrBadRequest(universe.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
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
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    String oldYbcVersion = universe.getUniverseDetails().getYbcSoftwareVersion();
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcNodeBatchSize))).thenReturn(1);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcUniverseBatchSize))).thenReturn(2);
    new YbcUpgrade(
            mockAppConfig,
            mockPlatformScheduler,
            mockConfGetter,
            mockYbcClientService,
            mockYbcManager,
            mockNodeUniverseManager,
            mockNodeManager)
        .scheduleRunner();
    Set<String> universeYbcVersions = new HashSet<>();
    universeYbcVersions.add(
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    universeYbcVersions.add(
        Universe.getOrBadRequest(universe.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
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
            defaultCustomer.getId(),
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
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcNodeBatchSize))).thenReturn(4);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcUniverseBatchSize))).thenReturn(2);
    new YbcUpgrade(
            mockAppConfig,
            mockPlatformScheduler,
            mockConfGetter,
            mockYbcClientService,
            mockYbcManager,
            mockNodeUniverseManager,
            mockNodeManager)
        .scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(universe.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeFailedUniverse() {
    when(mockYbcClient.Upgrade(any())).thenReturn(null);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeRequestOnUnreachableNodes() {
    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
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
    String oldYbcVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
    when(mockYbcClient2.Upgrade(any())).thenReturn(resp);
    when(mockYbcClient2.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(
            nodes.get(1).cloudInfo.private_ip,
            defaultUniverse.getUniverseDetails().communicationPorts.ybControllerrRpcPort,
            defaultUniverse.getCertificateNodetoNode()))
        .thenReturn(mockYbcClient2);
    ybcUpgrade.scheduleRunner();
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }

  @Test
  public void testDisabledScheduledUniverseUpgrade() {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybcAllowScheduledUpgrade)))
        .thenReturn(false);
    String oldYbcVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();
    ybcUpgrade.scheduleRunner();
    assertEquals(
        oldYbcVersion,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());

    UpgradeResponse resp =
        UpgradeResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.Upgrade(any())).thenReturn(resp);
    UpgradeResultResponse upgradeResultResponse =
        UpgradeResultResponse.newBuilder()
            .setStatus(ControllerStatus.COMPLETE)
            .setCurrentYbcVersion(NEW_YBC_VERSION)
            .build();
    when(mockYbcClient.UpgradeResult(any())).thenReturn(upgradeResultResponse);
    when(mockYbcClientService.getNewClient(any(), anyInt(), any())).thenReturn(mockYbcClient);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybcAllowScheduledUpgrade)))
        .thenReturn(true);
    ybcUpgrade.scheduleRunner();
    assertEquals(
        NEW_YBC_VERSION,
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getYbcSoftwareVersion());
  }
}
