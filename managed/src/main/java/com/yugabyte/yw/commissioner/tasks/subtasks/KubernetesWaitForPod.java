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
import io.fabric8.kubernetes.api.model.PodCondition;
import io.fabric8.kubernetes.api.model.PodStatus;
import java.time.Duration;
import java.util.Map;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class KubernetesWaitForPod extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(KubernetesWaitForPod.class);

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
        boolean podReady;
        do {
          try {
            podReady = isPodReady();
          } catch (Exception e) {
            LOG.info("Exception occurred (ignored) while waiting for pod: {}", e.getMessage());
            podReady = false;
          }
          iters++;
          if (podReady) {
            break;
          }

          waitFor(Duration.ofSeconds(getSleepMultiplier() * SLEEP_TIME));
        } while ((!podReady) && (iters < MAX_ITERS));
        if (iters > MAX_ITERS) {
          throw new RuntimeException("Pod " + taskParams().podName + " creation taking too long.");
        }
        break;
    }
  }

  // Waits for pods as well as the containers inside the pod.
  private boolean isPodReady() {
    Map<String, String> config = taskParams().config;
    if (taskParams().config == null) {
      Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
      config = CloudInfoInterface.fetchEnvVars(provider);
    }

    Pod podObject =
        kubernetesManagerFactory
            .getManager()
            .getPodObject(config, taskParams().namespace, taskParams().podName);
    if (podObject == null) {
      return false;
    }
    PodStatus podStatus = podObject.getStatus();

    // This is to verify that we are not getting pods that are already
    // marked for deletion but in Running state.
    if (podObject.getMetadata().getDeletionTimestamp() != null) {
      // Relevant post: https://issue.k8s.io/61376#issuecomment-374437926
      LOG.info("Pod has valid deletion timestamp");
      return false;
    }
    String status = podStatus.getPhase();
    if (!("Running".equalsIgnoreCase(status))) {
      return false;
    }

    for (PodCondition condition : podStatus.getConditions()) {
      if (condition.getStatus().equals("False")) {
        return false;
      }
    }

    return true;
  }
}
