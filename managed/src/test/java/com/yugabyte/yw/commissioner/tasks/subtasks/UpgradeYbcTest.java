package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.YBClient;

public class UpgradeYbcTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private final String TARGET_YBC_VERSION = "1.0.0-b2";
  private YBClient mockClient;

  @Before
  public void Setup() {
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
    mockClient = mock(YBClient.class);
    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getUniverseClient(any())).thenReturn(mockClient);
  }

  @Test
  public void testUpgradeSuccessOnlyMasterLeader() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    when(mockYbcUpgrade.getUniverseNodeYbcVersions(any(), anyBoolean()))
        .thenReturn(defaultUniverse.getNodes().stream().map(n -> TARGET_YBC_VERSION).toList());
    upgradeYbcTask.run();
    Universe postUpgradeUni = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(TARGET_YBC_VERSION, postUpgradeUni.getUniverseDetails().getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeSuccess() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyLiveNodes = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    when(mockYbcUpgrade.getUniverseNodeYbcVersions(any(), anyBoolean()))
        .thenReturn(defaultUniverse.getNodes().stream().map(n -> TARGET_YBC_VERSION).toList());
    upgradeYbcTask.run();
    Universe postUpgradeUni = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(TARGET_YBC_VERSION, postUpgradeUni.getUniverseDetails().getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeSuccessWithMasterOnlyNode() {
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams params = u.getUniverseDetails();
          NodeDetails masterOnly = new NodeDetails();
          masterOnly.nodeName = "yb-master-0";
          masterOnly.nodeUuid = UUID.randomUUID();
          masterOnly.cloudInfo = new CloudSpecificInfo();
          masterOnly.cloudInfo.private_ip = "10.0.0.99";
          masterOnly.isTserver = false;
          masterOnly.isMaster = true;
          params.nodeDetailsSet.add(masterOnly);
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyLiveNodes = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    List<String> tserverVersions =
        defaultUniverse.getNodes().stream()
            .filter(n -> n.isTserver)
            .map(n -> TARGET_YBC_VERSION)
            .toList();
    when(mockYbcUpgrade.getUniverseNodeYbcVersions(any(), anyBoolean()))
        .thenReturn(tserverVersions);
    upgradeYbcTask.run();
    Universe postUpgradeUni = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(TARGET_YBC_VERSION, postUpgradeUni.getUniverseDetails().getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeDoesNotStampWhenNoNodeRunsYbc() {
    UniverseUpdater updater =
        u ->
            u.getUniverseDetails()
                .nodeDetailsSet
                .forEach(
                    n -> {
                      n.isMaster = true;
                      n.isTserver = false;
                    });
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);
    String preUpgradeVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();

    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyLiveNodes = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    when(mockYbcUpgrade.getUniverseNodeYbcVersions(any(), anyBoolean())).thenReturn(List.of());

    upgradeYbcTask.run();

    // Nothing reported the new version, so the universe must not be marked as upgraded.
    Universe postUpgradeUni = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(preUpgradeVersion, postUpgradeUni.getUniverseDetails().getYbcSoftwareVersion());
  }

  @Test
  public void testUpgradeRequestFailure() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(false);
    RuntimeException re = assertThrows(RuntimeException.class, () -> upgradeYbcTask.run());
    assertThat(
        re.getMessage(), containsString("YBC Upgrade task did not complete in expected time."));
  }

  @Test
  public void testUpgradeNonYbcUniverse() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    Universe universe =
        defaultUniverse =
            ModelFactory.createUniverse(
                "Test-Universe-2",
                UUID.randomUUID(),
                defaultCustomer.getId(),
                CloudType.aws,
                null,
                null,
                false);
    params.universeUUID = universe.getUniverseUUID();
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(false);
    RuntimeException re = assertThrows(RuntimeException.class, () -> upgradeYbcTask.run());
    assertThat(
        re.getMessage(),
        containsString(
            "Cannot upgrade YBC as it is not enabled on universe " + universe.getUniverseUUID()));
  }

  @Test
  public void testUpgradeSameYbcVersion() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = defaultUniverse.getUniverseDetails().getYbcSoftwareVersion();
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(false);
    RuntimeException re = assertThrows(RuntimeException.class, () -> upgradeYbcTask.run());
    String errMsg =
        "YBC version "
            + params.ybcVersion
            + " is already installed on universe "
            + defaultUniverse.getUniverseUUID();
    assertThat(re.getMessage(), containsString(errMsg));
  }

  @Test
  public void testPartialUpgradeSuccess() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.getUniverseUUID();
    params.validateOnlyMasterLeader = true;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any(), anyBoolean());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    when(mockYbcClientService.getYbcServerVersion(any(), anyInt(), any()))
        .thenReturn(TARGET_YBC_VERSION);
    upgradeYbcTask.run();
  }
}
