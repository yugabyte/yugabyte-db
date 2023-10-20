// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest.waitForTask;
import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;

public class GFlagsUpgradeLocalTest extends LocalProviderUniverseTestBase {

  @Test
  public void testNonRestartUpgrade() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = new SpecificGFlags();
    Universe universe = createUniverse(userIntent);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    initYSQL(universe);
    verifyYSQL(universe);
  }

  @Test
  public void testNonRollingUpgrade() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = new SpecificGFlags();
    Universe universe = createUniverse(userIntent);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    initYSQL(universe);
    verifyYSQL(universe);
  }

  @Test
  public void testRollingUpgradeWithRRInherited() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    addReadReplica(universe, getDefaultUserIntent());

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    initYSQL(universe);
    verifyYSQL(universe);
  }

  @Test
  public void testRollingUpgradeWithRR() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    addReadReplica(universe, getDefaultUserIntent());

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    initYSQL(universe);
    verifyYSQL(universe);
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags)
      throws InterruptedException {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(universe, upgradeOption, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    if (rrGflags != null) {
      gFlagsUpgradeParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = rrGflags;
    }
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeGFlags(
                gFlagsUpgradeParams,
                customer,
                Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    return taskInfo;
  }

  private <T extends UpgradeTaskParams> T getUpgradeParams(
      Universe universe, UpgradeTaskParams.UpgradeOption upgradeOption, Class<T> clazz) {
    T upgadeParams = UniverseControllerRequestBinder.deepCopy(universe.getUniverseDetails(), clazz);
    upgadeParams.setUniverseUUID(universe.getUniverseUUID());
    upgadeParams.upgradeOption = upgradeOption;
    upgadeParams.expectedUniverseVersion = universe.getVersion();
    upgadeParams.clusters = universe.getUniverseDetails().clusters;
    return upgadeParams;
  }

  private void compareGFlags(Universe universe) {
    SpecificGFlags specificGFlags =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags;
    for (NodeDetails node : universe.getNodes()) {
      for (UniverseTaskBase.ServerType serverType : node.getAllProcesses()) {
        Map<String, String> varz = getVarz(node, universe, serverType);
        Map<String, String> gflags = specificGFlags.getGFlags(node, serverType);
        gflags.forEach(
            (k, v) -> {
              String actual = varz.getOrDefault(k, "?????");
              assertEquals(v, actual);
            });
      }
    }
  }

  private Map<String, String> getVarz(
      NodeDetails nodeDetails, Universe universe, UniverseTaskBase.ServerType serverType) {
    UniverseTaskParams.CommunicationPorts ports = universe.getUniverseDetails().communicationPorts;
    int port =
        serverType == UniverseTaskBase.ServerType.MASTER
            ? ports.masterHttpPort
            : ports.tserverHttpPort;
    JsonNode varz =
        nodeUIApiHelper.getRequest(
            "http://" + nodeDetails.cloudInfo.private_ip + ":" + port + "/api/v1/varz");
    Map<String, String> result = new HashMap<>();
    for (JsonNode flag : varz.get("flags")) {
      result.put(flag.get("name").asText(), flag.get("value").asText());
    }
    return result;
  }
}
