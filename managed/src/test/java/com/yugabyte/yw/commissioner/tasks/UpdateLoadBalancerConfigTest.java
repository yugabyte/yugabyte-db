// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Test;

public class UpdateLoadBalancerConfigTest extends UniverseModifyBaseTest {

  private static PlacementInfo.PlacementRegion primaryRegion(UniverseDefinitionTaskParams details) {
    return details.getPrimaryCluster().placementInfo.cloudList.get(0).regionList.get(0);
  }

  private TaskInfo submitWithLbFqdn(Universe universe, String lbFQDN) throws Exception {
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.setExistingLBs(taskParams.clusters);
    primaryRegion(taskParams).lbFQDN = lbFQDN;
    return waitForTask(commissioner.submit(TaskType.UpdateLoadBalancerConfig, taskParams));
  }

  @Test
  public void testSavesLbFqdnFromRequest() throws Exception {
    TaskInfo taskInfo = submitWithLbFqdn(defaultUniverse, "byo-lb.example.com");

    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals("byo-lb.example.com", primaryRegion(universe.getUniverseDetails()).lbFQDN);
  }
}
