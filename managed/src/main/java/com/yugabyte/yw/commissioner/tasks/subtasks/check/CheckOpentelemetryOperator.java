/*
 * Copyright 2025 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckOpentelemetryOperator extends KubernetesTaskBase {

  private final ShellKubernetesManager shellKubernetesManager;

  @Inject
  protected CheckOpentelemetryOperator(
      BaseTaskDependencies baseTaskDependencies, ShellKubernetesManager shellKubernetesManager) {
    super(baseTaskDependencies);
    this.shellKubernetesManager = shellKubernetesManager;
  }

  @Override
  public UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      shellKubernetesManager.checkOpentelemetryOperatorRunning();
    } catch (Exception e) {
      log.error("Error executing task {} with error={}.", getName(), e.getMessage());
      throw e;
    }
    log.info("Opentelemetry collector is installed.");
  }
}
