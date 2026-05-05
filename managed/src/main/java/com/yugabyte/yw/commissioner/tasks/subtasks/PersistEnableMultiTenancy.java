// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.MultiTenancyConfig;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PersistEnableMultiTenancy extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(PersistEnableMultiTenancy.class);

  public static class Params extends UniverseTaskParams {
    public MultiTenancyConfig multiTenancy;
  }

  @Inject
  public PersistEnableMultiTenancy(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().getUniverseUUID()
        + ", multiTenancy: "
        + taskParams().multiTenancy
        + ")";
  }

  @Override
  public void run() {
    try {
      LOG.info("Running {}", getName());

      final MultiTenancyConfig config = taskParams().multiTenancy;
      LOG.debug("Setting multiTenancy: {} in universe: {}", config, taskParams().getUniverseUUID());
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getPrimaryCluster().userIntent.setMultiTenancy(config);
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
