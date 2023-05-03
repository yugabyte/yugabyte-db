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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.fabric8.kubernetes.api.model.Pod;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public class KubernetesCheckNumPod extends AbstractTaskBase {
  public enum CommandType {
    WAIT_FOR_PODS;

    public String getSubTaskGroupName() {
      switch (this) {
        case WAIT_FOR_PODS:
          return UserTaskDetails.SubTaskGroupType.KubernetesCheckNumPod.name();
      }
      return null;
    }
  }

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesCheckNumPod(
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
    public String helmReleaseName;
    public String namespace;
    public int podNum = 0;
    public Map<String, String> config = null;
  }

  protected KubernetesCheckNumPod.Params taskParams() {
    return (KubernetesCheckNumPod.Params) taskParams;
  }

  @Override
  public void run() {
    // TODO: add checks for the shell process handler return values.
    switch (taskParams().commandType) {
      case WAIT_FOR_PODS:
        int iters = 0;
        boolean status;
        do {
          status = waitForPods();
          iters++;
          if (status) {
            break;
          }
          waitFor(Duration.ofSeconds(getSleepMultiplier() * SLEEP_TIME));
        } while (!status && iters < MAX_ITERS);
        if (iters >= MAX_ITERS) {
          throw new RuntimeException("Pods' start taking too long.");
        }
        break;
    }
  }

  // Wait for the correct number of pods to be in the call.
  private boolean waitForPods() {
    Map<String, String> config = taskParams().config;
    if (taskParams().config == null) {
      Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
      config = CloudInfoInterface.fetchEnvVars(provider);
    }
    List<Pod> pods =
        kubernetesManagerFactory
            .getManager()
            .getPodInfos(config, taskParams().helmReleaseName, taskParams().namespace);
    if (pods.size() == taskParams().podNum) {
      return true;
    } else {
      return false;
    }
  }
}
