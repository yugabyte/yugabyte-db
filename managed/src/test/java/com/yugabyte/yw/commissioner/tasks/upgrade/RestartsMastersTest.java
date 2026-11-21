// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.CALLS_REAL_METHODS;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.MastersAndTservers;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.List;
import org.junit.Test;

/** Decides which tasks get the KMS universe key precheck. */
public class RestartsMastersTest {

  private static NodeDetails node(boolean isMaster) {
    NodeDetails node = new NodeDetails();
    node.isMaster = isMaster;
    return node;
  }

  @Test
  public void testMasterNodeRestartsMasters() {
    MastersAndTservers nodes = new MastersAndTservers(List.of(node(true)), List.of(node(true)));
    assertTrue(UpgradeTaskBase.restartsMasters(UpgradeOption.ROLLING_UPGRADE, nodes));
    assertTrue(UpgradeTaskBase.restartsMasters(UpgradeOption.NON_ROLLING_UPGRADE, nodes));
    assertFalse(UpgradeTaskBase.restartsMasters(UpgradeOption.NON_RESTART_UPGRADE, nodes));
  }

  // ResizeNode puts tserver-only nodes (read replicas, dedicated tserver nodes) in mastersList.
  @Test
  public void testTserverOnlyNodeInMastersListDoesNotRestartMasters() {
    MastersAndTservers nodes = new MastersAndTservers(List.of(node(false)), List.of(node(false)));
    assertFalse(UpgradeTaskBase.restartsMasters(UpgradeOption.ROLLING_UPGRADE, nodes));
  }

  @Test
  public void testNoNodesDoesNotRestartMasters() {
    assertFalse(UpgradeTaskBase.restartsMasters(UpgradeOption.ROLLING_UPGRADE, null));
    assertFalse(
        UpgradeTaskBase.restartsMasters(
            UpgradeOption.ROLLING_UPGRADE,
            new MastersAndTservers(Collections.emptyList(), Collections.emptyList())));
  }

  // These tasks list masters in nodesToBeRestarted but never restart one.
  @Test
  public void testTasksThatNeverRestartMastersSkipCheck() {
    assertFalse(mock(FinalizeKubernetesUpgrade.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
    assertFalse(mock(UpgradeKubernetesYbcGFlags.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
    assertFalse(
        mock(KubernetesToggleImmutableYbc.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
    assertFalse(mock(ModifyQueryLoggingConfig.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
    assertFalse(mock(ModifyAuditLoggingConfig.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
    assertFalse(mock(ModifyMetricsExportConfig.class, CALLS_REAL_METHODS).needsUniverseKeyCheck());
  }
}
