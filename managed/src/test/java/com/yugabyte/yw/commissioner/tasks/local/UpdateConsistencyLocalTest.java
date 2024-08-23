// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.CONSISTENCY_CHECK;
import static com.yugabyte.yw.common.Util.SYSTEM_PLATFORM_DB;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

@Slf4j
public class UpdateConsistencyLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(120, 150);
  }

  @Test
  public void testUpdateStale() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    NodeDetails details = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ShellResponse ysqlResponse =
        localNodeUniverseManager.runYsqlCommand(
            details,
            universe,
            SYSTEM_PLATFORM_DB,
            String.format("update %s set seq_num = 10", CONSISTENCY_CHECK),
            10);
    assertTrue(ysqlResponse.isSuccess());
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = waitForTask(taskID, universe);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("stale metadata"));
  }
}
