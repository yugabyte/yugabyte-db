package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckBootstrapRequired extends XClusterConfigTaskBase {

  @Inject
  protected CheckBootstrapRequired(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table ids to check whether each of them needs bootstrap.
    public Set<String> tableIds;
  }

  @Override
  protected CheckBootstrapRequired.Params taskParams() {
    return (CheckBootstrapRequired.Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(sourceUniverse=%s,xClusterUuid=%s,tableIds=%s)",
        super.getName(),
        taskParams().universeUUID,
        taskParams().xClusterConfig,
        taskParams().tableIds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    checkBootstrapRequired(taskParams().tableIds);

    log.info("Completed {}", getName());
  }
}
