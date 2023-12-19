/*
* Copyright 2023 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.Map;
import java.util.UUID;

public class KubernetesPostExpansionCheckVolume extends AbstractTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesPostExpansionCheckVolume(
      BaseTaskDependencies baseTaskDependencies,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  public static class Params extends AbstractTaskParams {
    public Map<String, String> config;
    public boolean newNamingStyle;
    public String namespace;
    public UUID providerUUID;
    public String newDiskSizeGi;
    public String helmReleaseName;
  }

  public static String getSubTaskGroupName() {
    return SubTaskGroupType.KubernetesVolumeInfo.name();
  }

  protected KubernetesPostExpansionCheckVolume.Params taskParams() {
    return (KubernetesPostExpansionCheckVolume.Params) taskParams;
  }

  @Override
  public void run() {
    Map<String, String> config = taskParams().config;
    if (config == null) {
      Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
      config = CloudInfoInterface.fetchEnvVars(provider);
    }

    if (KubernetesUtil.needsExpandPVC(
        taskParams().namespace,
        taskParams().helmReleaseName,
        "yb-tserver",
        taskParams().newNamingStyle,
        taskParams().newDiskSizeGi,
        taskParams().config,
        kubernetesManagerFactory)) {
      throw new RuntimeException(
          String.format(
              "PVC expansion attempt to %s has failed in namespace %s",
              taskParams().newDiskSizeGi, taskParams().namespace));
    }
  }
}
