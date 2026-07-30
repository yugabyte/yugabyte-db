// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;

import api.v2.mappers.DrConfigEnumMapper;
import api.v2.models.DrConfigInfo;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameters;

@RunWith(Parameterized.class)
public class DrConfigEnumMapperTest {

  private final XClusterConfigStatusType xClusterStatus;
  private final State drState;
  private final DrConfigInfo.StateEnum expected;

  public DrConfigEnumMapperTest(
      XClusterConfigStatusType xClusterStatus, State drState, DrConfigInfo.StateEnum expected) {
    this.xClusterStatus = xClusterStatus;
    this.drState = drState;
    this.expected = expected;
  }

  @Parameters(name = "xCluster={0}, drState={1} -> {2}")
  public static Object[][] combinedStateCases() {
    return new Object[][] {
      {
        XClusterConfigStatusType.DeletedUniverse,
        State.Replicating,
        DrConfigInfo.StateEnum.DELETED_UNIVERSE
      },
      {
        XClusterConfigStatusType.DeletionFailed,
        State.Replicating,
        DrConfigInfo.StateEnum.DELETION_FAILED
      },
      {
        XClusterConfigStatusType.Running,
        State.SwitchoverInProgress,
        DrConfigInfo.StateEnum.SWITCHOVER_IN_PROGRESS
      },
      {
        XClusterConfigStatusType.Updating,
        State.SwitchoverInProgress,
        DrConfigInfo.StateEnum.SWITCHOVER_IN_PROGRESS
      },
      {
        XClusterConfigStatusType.Running,
        State.FailoverInProgress,
        DrConfigInfo.StateEnum.FAILOVER_IN_PROGRESS
      },
      {XClusterConfigStatusType.Running, State.Halted, DrConfigInfo.StateEnum.HALTED},
      {XClusterConfigStatusType.Running, State.Initializing, DrConfigInfo.StateEnum.INITIALIZING},
      {
        XClusterConfigStatusType.Initialized,
        State.Initializing,
        DrConfigInfo.StateEnum.INITIALIZING
      },
      {XClusterConfigStatusType.Running, State.Paused, DrConfigInfo.StateEnum.PAUSED},
      {XClusterConfigStatusType.Running, State.Failed, DrConfigInfo.StateEnum.FAILED},
      {XClusterConfigStatusType.Failed, State.Replicating, DrConfigInfo.StateEnum.FAILED},
      {XClusterConfigStatusType.Running, State.Updating, DrConfigInfo.StateEnum.UPDATING},
      {XClusterConfigStatusType.Updating, State.Replicating, DrConfigInfo.StateEnum.UPDATING},
      {XClusterConfigStatusType.Initialized, State.Replicating, DrConfigInfo.StateEnum.UPDATING},
      {XClusterConfigStatusType.Running, State.Replicating, DrConfigInfo.StateEnum.REPLICATING},
    };
  }

  @Test
  public void toCombinedStateEnum() {
    assertEquals(
        expected, DrConfigEnumMapper.INSTANCE.toCombinedStateEnum(xClusterStatus, drState));
  }
}
