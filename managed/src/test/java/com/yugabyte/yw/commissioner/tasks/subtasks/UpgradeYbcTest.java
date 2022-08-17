package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertNull;
import static org.hamcrest.MatcherAssert.assertThat;
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
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    mockClient = mock(YBClient.class);
    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
  }

  @Test
  public void testUpgradeSuccess() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(true);
    when(mockYbcUpgrade.getUniverseYbcVersion(any())).thenReturn(TARGET_YBC_VERSION);
    upgradeYbcTask.run();
  }

  @Test
  public void testUpgradeRequestFailure() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(false);
    RuntimeException re = assertThrows(RuntimeException.class, () -> upgradeYbcTask.run());
    assertThat(
        re.getMessage(),
        containsString("YBC Upgrade task failed as ybc does not upgraded on master leader."));
  }

  @Test
  public void testUpgradeNonYbcUniverse() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    Universe universe =
        defaultUniverse =
            ModelFactory.createUniverse(
                "Test-Universe-2",
                UUID.randomUUID(),
                defaultCustomer.getCustomerId(),
                CloudType.aws,
                null,
                null,
                false);
    params.universeUUID = universe.universeUUID;
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any());
    } catch (Exception e) {
      assertNull(e);
    }
    when(mockYbcUpgrade.checkYBCUpgradeProcessExists(any())).thenReturn(false);
    when(mockYbcUpgrade.pollUpgradeTaskResult(any(), any(), anyBoolean())).thenReturn(false);
    RuntimeException re = assertThrows(RuntimeException.class, () -> upgradeYbcTask.run());
    assertThat(
        re.getMessage(),
        containsString(
            "Cannot upgrade YBC as it is not enabled on universe " + universe.universeUUID));
  }

  @Test
  public void testUpgradeSameYbcVersion() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.validateOnlyMasterLeader = false;
    params.ybcVersion = defaultUniverse.getUniverseDetails().ybcSoftwareVersion;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any());
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
            + defaultUniverse.universeUUID;
    assertThat(re.getMessage(), containsString(errMsg));
  }

  @Test
  public void testPartialUpgradeSuccess() {
    UpgradeYbc.Params params = new UpgradeYbc.Params();
    params.universeUUID = defaultUniverse.universeUUID;
    params.validateOnlyMasterLeader = true;
    params.ybcVersion = TARGET_YBC_VERSION;
    UpgradeYbc upgradeYbcTask = AbstractTaskBase.createTask(UpgradeYbc.class);
    upgradeYbcTask.initialize(params);
    try {
      doNothing().when(mockYbcUpgrade).upgradeYBC(any(), any());
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
