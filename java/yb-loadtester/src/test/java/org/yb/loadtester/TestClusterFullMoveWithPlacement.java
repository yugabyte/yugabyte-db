package org.yb.loadtester;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.ModifyClusterConfigAffinitizedLeaders;
import org.yb.YBTestRunner;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value= YBTestRunner.class)
public class TestClusterFullMoveWithPlacement extends TestClusterBase {

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    flags.put("placement_cloud", "c");
    flags.put("placement_region", "r");
    flags.put("placement_zone", "z");
    flags.put("TEST_crash_server_on_sys_catalog_leader_affinity_move", "true");
    flags.put("sys_catalog_respect_affinity_task", "true");
    return flags;
  }


  @Test(timeout = TEST_TIMEOUT_SEC * 1000)
  public void testSysCatalogAffinityWithFullMove() throws Exception {
    List<org.yb.CommonNet.CloudInfoPB> leaders = new ArrayList<org.yb.CommonNet.CloudInfoPB>();

    org.yb.CommonNet.CloudInfoPB cloudInfo = org.yb.CommonNet.CloudInfoPB.newBuilder().
            setPlacementCloud("c").setPlacementRegion("r").setPlacementZone("z").build();

    leaders.add(cloudInfo);

    ModifyClusterConfigAffinitizedLeaders operation =
            new ModifyClusterConfigAffinitizedLeaders(client, leaders);
    try {
      operation.doCall();
    } catch (Exception e) {
      assertTrue(false);
    }

    performFullMasterMove(getMasterFlags());

    verifyClusterHealth();
  }
}
