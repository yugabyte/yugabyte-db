/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.Collections;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UpdateUniverseConfig extends UniverseTaskBase {

  @Inject
  protected UpdateUniverseConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  /**
   * Wrapper object to hold the Universe configuration
   *
   * @author msoundar
   */
  public static class Params extends UniverseTaskParams {
    /** Configurations that needs to be added/updated in the universe */
    public Map<String, String> configs;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());

      Universe universe = getUniverse();
      Map<String, String> newConfigs =
          taskParams() != null && taskParams().configs != null
              ? taskParams().configs
              : Collections.emptyMap();

      if (newConfigs.isEmpty()) {
        log.warn("Nothing to update on the Universe !");
        return;
      }
      log.info("Updating universe configuration {} with {}", universe.getConfig(), newConfigs);
      universe.updateConfig(newConfigs);
      universe.save();
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
