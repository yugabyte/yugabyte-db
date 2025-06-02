// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
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

  public static final String OLD_VERSION_WITH_ROLLBACK = "2.21.0.0-b340";
  private static final String OLD_VERSION_WTH_ROLLBACK_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.21.0.0-b340/yugabyte-2.21.0.0-b340-%s-%s.tar.gz";

  private static final String PG_15_DB_VERSION = "2.25.1.0-b381";
  private static final String PG_15_DB_VERSION_URL =
      "https://software.yugabyte.com/releases/2.25.1.0/yugabyte-2.25.1.0-b381-%s-%s.tar.gz";

  private static final String PG_11_DB_VERSION = "2024.2.3.0-b116";
  private static final String PG_11_DB_VERSION_URL =
      "https://software.yugabyte.com/releases/2024.2.3.0/yugabyte-2024.2.3.0-b116-%s-%s.tar.gz";

  @Before
  public void setup() {
    addRelease(OLD_VERSION_WITH_ROLLBACK, OLD_VERSION_WTH_ROLLBACK_URL);
    addRelease(OLD_DB_VERSION, OLD_DB_VERSION_URL);
    addRelease(PG_11_DB_VERSION, PG_11_DB_VERSION_URL);
    addRelease(PG_15_DB_VERSION, PG_15_DB_VERSION_URL);
    localNodeManager.addVersionBinPath(
        OLD_VERSION_WITH_ROLLBACK,
        baseDir + "/yugabyte/yugabyte-" + OLD_VERSION_WITH_ROLLBACK + "/bin");
    localNodeManager.addVersionBinPath(
        OLD_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + OLD_DB_VERSION + "/bin");

    localNodeManager.addVersionBinPath(
        PG_15_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_15_DB_VERSION + "/bin");
    localNodeManager.addVersionBinPath(
        PG_11_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_11_DB_VERSION + "/bin");

    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.skipVersionChecks.getKey(),
        "true",
        true);
  }

  protected SoftwareUpgradeParams getBaseUpgradeParams() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.expectedUniverseVersion = -1;
    params.sleepAfterMasterRestartMillis = 10000;
    params.sleepAfterTServerRestartMillis = 10000;
    return params;
  }

  @Test
  public void testSoftwareUpgradeWithNoRollbackSupport() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_DB_VERSION;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = PG_11_DB_VERSION;
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
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
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
  public void testFinalizeUpgrade() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
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

  @Test
  public void testPG15SoftwareUpgradeRollback() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(PG_11_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = PG_11_DB_VERSION;
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowDowngrades.getKey(),
        "true",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_15_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
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
  public void testPG15SoftwareUpgradeFinalize() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(PG_11_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = PG_11_DB_VERSION;
    userIntent.enableYSQL = true;
    userIntent.ysqlPassword = "password&123";
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowDowngrades.getKey(),
        "true",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_15_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
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

  private void addRelease(String dbVersion, String dbVersionUrl) {
    String downloadURL = String.format(dbVersionUrl, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  private void updateProviderDetailsForCreateUniverse(String dbVersion) {
    ybVersion = dbVersion;
    ybBinPath = deriveYBBinPath(dbVersion);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(
        ((LocalCloudInfo) CloudInfoInterface.get(provider)).getDataHomeDir());
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    localCloudInfo.setYbcBinDir(ybcBinPath);
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    ProviderDetails providerDetails = new ProviderDetails();
    providerDetails.setCloudInfo(cloudInfo);
    provider.setDetails(providerDetails);
    provider.update();
  }
}
