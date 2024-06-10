// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class YbcControllerTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Users defaultUser;
  private Universe defaultYbcUniverse;
  private Universe defaultNonYbcUniverse;

  @Before
  public void setUp() throws IOException {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultYbcUniverse =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    defaultNonYbcUniverse =
        ModelFactory.createUniverse(
            "Test-Universe-2",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            false);
  }

  @Test
  public void testDisableYbcSuccess() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.DisableYbc);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = disableYbc(defaultYbcUniverse.getUniverseUUID());
    assertOk(result);
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDisableYbcOnNonYbcUniverse() {
    Result result =
        assertPlatformException(() -> disableYbc(defaultNonYbcUniverse.getUniverseUUID()));
    assertBadRequest(
        result,
        "Ybc is either not installed or enabled on universe: "
            + defaultNonYbcUniverse.getUniverseUUID());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testDisableYbcOnUniverseNodesInTransit() {
    setUniverseNodesInTransit(defaultYbcUniverse);
    Result result = assertPlatformException(() -> disableYbc(defaultYbcUniverse.getUniverseUUID()));
    assertBadRequest(
        result,
        "Cannot disable ybc on universe "
            + defaultYbcUniverse.getUniverseUUID()
            + " as it has nodes in one of "
            + "[Removed, Stopped, Decommissioned, Resizing, Terminated] states.");
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  @Parameters({"", "1.0.0-b2"})
  public void testUpgradeYbcSuccess(String ybcVersion) {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.UpgradeYbc);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = upgradeYbc(defaultYbcUniverse.getUniverseUUID(), ybcVersion);
    assertOk(result);
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testUpgradeYbcOnUniverseNodesInTransit() {
    setUniverseNodesInTransit(defaultYbcUniverse);
    Result result =
        assertPlatformException(() -> upgradeYbc(defaultYbcUniverse.getUniverseUUID(), null));
    assertBadRequest(
        result,
        "Cannot perform a ybc upgrade on universe "
            + defaultYbcUniverse.getUniverseUUID()
            + " as it has nodes in one of "
            + "[Removed, Stopped, Decommissioned, Resizing, Terminated] states.");
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testUpgradeYbcOnNonYbcUniverse() {
    Result result =
        assertPlatformException(() -> upgradeYbc(defaultNonYbcUniverse.getUniverseUUID(), null));
    assertBadRequest(
        result,
        "Ybc is either not installed or enabled on universe: "
            + defaultNonYbcUniverse.getUniverseUUID());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testUpgradeExistingYbcVersion() {
    String targetYbcVersion = defaultYbcUniverse.getUniverseDetails().getYbcSoftwareVersion();
    Result result =
        assertPlatformException(
            () -> upgradeYbc(defaultYbcUniverse.getUniverseUUID(), targetYbcVersion));
    assertBadRequest(
        result,
        "Ybc version "
            + targetYbcVersion
            + " is already present on universe "
            + defaultYbcUniverse.getUniverseUUID());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  @Parameters({"", "1.0.0-b2"})
  public void testInstallYbcSuccess(String ybcVersion) {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.InstallYbcSoftware);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UserIntent userIntent =
        defaultNonYbcUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = "2.15.0.0-b2";
    Universe.saveDetails(
        defaultNonYbcUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    Result result = installYbc(defaultNonYbcUniverse.getUniverseUUID(), ybcVersion);
    assertOk(result);
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testInstallYbcFailureWithNonCompatibleDBVersion() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UserIntent userIntent =
        defaultNonYbcUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = "2.13.0.0-b1";
    Universe.saveDetails(
        defaultNonYbcUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    Result result =
        assertPlatformException(
            () -> installYbc(defaultNonYbcUniverse.getUniverseUUID(), "1.0.0-b2"));
    assertBadRequest(result, "Cannot install universe with DB version lower than 2.15.0.0-b1");
    verify(mockCommissioner, times(0)).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testInstallYbcOnUniverseNodesInTransit() {
    setUniverseNodesInTransit(defaultYbcUniverse);
    Result result =
        assertPlatformException(() -> installYbc(defaultYbcUniverse.getUniverseUUID(), null));
    assertBadRequest(
        result,
        "Cannot perform a ybc installation on universe "
            + defaultYbcUniverse.getUniverseUUID()
            + " as it has nodes in one of "
            + "[Removed, Stopped, Decommissioned, Resizing, Terminated] states.");
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  private void setUniverseNodesInTransit(Universe universe) {
    UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          for (NodeDetails nodes : universeDetails.nodeDetailsSet) {
            nodes.state = NodeState.Decommissioned;
          }
        };
    Universe.saveDetails(universe.getUniverseUUID(), updater, false);
  }

  private Result disableYbc(UUID universeUUID) {
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/ybc/disable";
    return doRequestWithAuthToken("PUT", url, defaultUser.createAuthToken());
  }

  private Result upgradeYbc(UUID universeUUID, String ybcVersion) {
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/ybc/upgrade";
    if (!StringUtils.isEmpty(ybcVersion)) {
      url += "?ybcVersion=" + ybcVersion;
    }
    return doRequestWithAuthToken("PUT", url, defaultUser.createAuthToken());
  }

  private Result installYbc(UUID universeUUID, String ybcVersion) {
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/ybc/install";
    if (!StringUtils.isEmpty(ybcVersion)) {
      url += "?ybcVersion=" + ybcVersion;
    }
    return doRequestWithAuthToken("PUT", url, defaultUser.createAuthToken());
  }
}
