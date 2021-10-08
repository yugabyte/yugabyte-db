package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class EditXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected EditXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfigEditFormData editFormData = taskParams().editFormData;
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    xClusterConfig.updateFrom(editFormData);

    // TODO: Implement

    log.info("Done {}", getName());
  }
}
