package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected CreateXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // TODO: Implement

    log.info("Done {}", getName());
  }
}
