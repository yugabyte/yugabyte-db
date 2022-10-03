package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.RestoreSnapshotParams;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.time.Duration;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.RestoreSnapshotResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

@Slf4j
public class RestoreSnapshot extends UniverseTaskBase {

  private static final int WAIT_DURATION_IN_MS = 15000;

  @Inject
  protected RestoreSnapshot(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestoreSnapshotParams taskParams() {
    return (RestoreSnapshotParams) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().universeUUID
        + ", snapshotUUID="
        + taskParams().snapshotUUID
        + ")";
  }

  @Override
  public void run() {
    throw new RuntimeException("Restore snapshot task is work in progress");
  }
}
