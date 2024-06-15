// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.TlsConfigUpdateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class TLSToggleTest extends LocalProviderUniverseTestBase {

  private Result toggleTLS(TlsConfigUpdateParams formData, UUID universeUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID + "/update_tls",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(270, 300);
  }

  @Test
  public void testEnableTLS() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent("universe", true);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    verifyYSQL(universe);

    TlsConfigUpdateParams tlsConfigParams = new TlsConfigUpdateParams();
    tlsConfigParams.createNewRootCA = true;
    tlsConfigParams.enableNodeToNodeEncrypt = true;
    tlsConfigParams.enableClientToNodeEncrypt = true;
    tlsConfigParams.rootAndClientRootCASame = true;
    tlsConfigParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    Result result = toggleTLS(tlsConfigParams, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        true, universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        true,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);

    NodeDetails details = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            universe,
            YUGABYTE_DB,
            "insert into some_table values (4, 'tls1', 200), (5, 'tls2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2000);
    details = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, universe, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
  }

  // @Test
  public void testDisableTLS() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    verifyYSQL(universe);

    TlsConfigUpdateParams tlsConfigParams = new TlsConfigUpdateParams();
    tlsConfigParams.enableNodeToNodeEncrypt = false;
    tlsConfigParams.enableClientToNodeEncrypt = false;
    tlsConfigParams.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;

    Result result = toggleTLS(tlsConfigParams, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        false,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        false,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);

    NodeDetails details = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            universe,
            YUGABYTE_DB,
            "insert into some_table values (4, 'tls1', 200), (5, 'tls2', 180)",
            10);
    assertTrue(ysqlResponse.isSuccess());

    Thread.sleep(2000);
    details = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details, universe, YUGABYTE_DB, "select count(*) from some_table", 10);
    assertTrue(ysqlResponse.isSuccess());
    assertEquals("5", LocalNodeManager.getRawCommandOutput(ysqlResponse.getMessage()));
  }
}
