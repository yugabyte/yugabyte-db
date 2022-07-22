/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Provider;
import io.fabric8.kubernetes.api.model.PodCondition;
import io.fabric8.kubernetes.api.model.PodStatus;
import java.time.Duration;
import java.util.Map;
import java.util.UUID;

public class KubernetesWaitForPod extends AbstractTaskBase {
  public enum CommandType {
    WAIT_FOR_POD;

    public String getSubTaskGroupName() {
      switch (this) {
        case WAIT_FOR_POD:
          return UserTaskDetails.SubTaskGroupType.KubernetesWaitForPod.name();
      }
      return null;
    }
  }

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesWaitForPod(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  // Number of iterations to wait for the pod to come up.
  private static final int MAX_ITERS = 10;

  // Time to sleep on each iteration of the pod to come up.
  private static final int SLEEP_TIME = 10;

  public static class Params extends AbstractTaskParams {
    public UUID providerUUID;
    public CommandType commandType;
    public UUID universeUUID;
    // TODO(bhavin192): helmReleaseName can be removed as we are not
    // doing any sort of Helm operation here. Or we might want to use
    // it for some sort of label based selection.
    public String helmReleaseName;
    public String namespace;
    public String podName = null;
    public Map<String, String> config = null;
  }

  protected KubernetesWaitForPod.Params taskParams() {
    return (KubernetesWaitForPod.Params) taskParams;
  }

  @Override
  public void run() {
    // TODO: add checks for the shell process handler return values.
    switch (taskParams().commandType) {
      case WAIT_FOR_POD:
        int iters = 0;
        String status;
        do {
          status = waitForPod();
          iters++;
          if (status.equals("Running")) {
            break;
          }
          waitFor(Duration.ofSeconds(getSleepMultiplier() * SLEEP_TIME));
        } while (!status.equals("Running") && iters < MAX_ITERS);
        if (iters > MAX_ITERS) {
          throw new RuntimeException("Pod " + taskParams().podName + " creation taking too long.");
        }
        break;
    }
  }

  // Waits for pods as well as the containers inside the pod.
  private String waitForPod() {
    Map<String, String> config = taskParams().config;
    if (taskParams().config == null) {
      config = Provider.get(taskParams().providerUUID).getUnmaskedConfig();
    }
    PodStatus podStatus =
        kubernetesManagerFactory
            .getManager()
            .getPodStatus(config, taskParams().namespace, taskParams().podName);
    String status = podStatus.getPhase();
    for (PodCondition condition : podStatus.getConditions()) {
      if (condition.getStatus().equals("False")) {
        status = "Not Ready";
      }
    }
    return status;
  }
}
