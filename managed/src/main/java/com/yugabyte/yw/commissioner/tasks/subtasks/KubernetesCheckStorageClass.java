/*
* Copyright 2022 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.base.Strings;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.Map;
import java.util.UUID;

import lombok.extern.slf4j.Slf4j;

@Slf4j
public class KubernetesCheckStorageClass extends AbstractTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesCheckStorageClass(
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
    public String helmReleaseName;
  }

  public static String getSubTaskGroupName() {
    return SubTaskGroupType.KubernetesVolumeInfo.name();
  }

  protected KubernetesCheckStorageClass.Params taskParams() {
    return (KubernetesCheckStorageClass.Params) taskParams;
  }

  @Override
  public void run() {
    KubernetesManager k8s = kubernetesManagerFactory.getManager();
    Map<String, String> config = taskParams().config;
    if (config == null) {
      config = Provider.getOrBadRequest(taskParams().providerUUID).getUnmaskedConfig();
    }

    // storage class name used by tserver
    String scName =
        k8s.getStorageClassName(
            taskParams().config,
            taskParams().namespace,
            taskParams().helmReleaseName,
            false,
            taskParams().newNamingStyle);
    if (Strings.isNullOrEmpty(scName)) {
      // Could be using ephemeral volume
      throw new RuntimeException("TServer Volume does not support expansion");
    }
    boolean allowsExpansion = k8s.storageClassAllowsExpansion(taskParams().config, scName);
    if (!allowsExpansion) {
      throw new RuntimeException(
          String.format(
              "StorageClass {} should allow volume expansion for this operation", scName));
    }
    log.info("Verified that the PVC storage class allows volume expansion");
  }
}
