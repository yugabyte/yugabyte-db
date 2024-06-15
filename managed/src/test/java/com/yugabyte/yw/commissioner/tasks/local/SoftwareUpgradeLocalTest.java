// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest.waitForTask;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import org.junit.Before;
import org.junit.Test;

public class SoftwareUpgradeLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(180, 210);
  }

  public static final String OLD_DB_VERSION = "2.20.0.1-b1";
  public static String OLD_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.20.0.1-b1/yugabyte-2.20.0.1-b1-%s-%s.tar.gz";

  public static final String NEW_DB_VERSION = "2.21.0.0-b366";
  public static final String NEW_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.21.0.0-b366/yugabyte-2.21.0.0-b366-%s-%s.tar.gz";

  public static final String OLD_VERSION_WITH_ROLLBACK = "2.21.0.0-b340";
  private static final String OLD_VERSION_WTH_ROLLBACK_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.21.0.0-b340/yugabyte-2.21.0.0-b340-%s-%s.tar.gz";

  @Before
  public void setup() {
    downloadAndSetUpYBSoftware(
        os, arch, String.format(NEW_DB_VERSION_URL, os, arch), NEW_DB_VERSION);

    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(NEW_DB_VERSION, getMetadataJson(NEW_DB_VERSION, false).get(NEW_DB_VERSION));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
    localNodeManager.addVersionBinPath(
        NEW_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + NEW_DB_VERSION + "/bin");
    localNodeManager.addVersionBinPath(
        OLD_VERSION_WITH_ROLLBACK,
        baseDir + "/yugabyte/yugabyte-" + OLD_VERSION_WITH_ROLLBACK + "/bin");
    localNodeManager.addVersionBinPath(
        OLD_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + OLD_DB_VERSION + "/bin");
  }

  @Test
  public void testSoftwareUpgradeWithNoRollbackSupport() throws InterruptedException {
    String downloadURL = String.format(OLD_DB_VERSION_URL, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, OLD_DB_VERSION);
    ybVersion = OLD_DB_VERSION;
    ybBinPath = deriveYBBinPath(OLD_DB_VERSION);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(OLD_DB_VERSION, getMetadataJson(OLD_DB_VERSION, false).get(OLD_DB_VERSION));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.expectedUniverseVersion = -1;
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
  }

  @Test
  public void testRollbackUpgrade() throws InterruptedException {
    addRelease(OLD_VERSION_WITH_ROLLBACK, String.format(OLD_VERSION_WTH_ROLLBACK_URL, os, arch));
    ybVersion = OLD_VERSION_WITH_ROLLBACK;
    ybBinPath = deriveYBBinPath(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = NEW_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.expectedUniverseVersion = -1;
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    taskInfo =
        waitForTask(
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
  }

  @Test
  public void finalizeUpgrade() throws InterruptedException {
    addRelease(OLD_VERSION_WITH_ROLLBACK, String.format(OLD_VERSION_WTH_ROLLBACK_URL, os, arch));
    ybVersion = OLD_VERSION_WITH_ROLLBACK;
    ybBinPath = deriveYBBinPath(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.ybSoftwareVersion = NEW_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.expectedUniverseVersion = -1;
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      FinalizeUpgradeParams finalizeUpgradeParams = new FinalizeUpgradeParams();
      finalizeUpgradeParams.setUniverseUUID(universe.getUniverseUUID());
      finalizeUpgradeParams.expectedUniverseVersion = -1;
      taskInfo =
          waitForTask(
              upgradeUniverseHandler.finalizeUpgrade(
                  finalizeUpgradeParams,
                  customer,
                  Universe.getOrBadRequest(universe.getUniverseUUID())));
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
      assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    }
  }

  private void addRelease(String dbVersion, String dbVersionUrl) {
    downloadAndSetUpYBSoftware(os, arch, dbVersionUrl, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }
}
