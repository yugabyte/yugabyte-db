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
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaim;
import io.fabric8.kubernetes.api.model.PersistentVolumeClaimVolumeSource;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.Volume;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class KubernetesCheckVolumeExpansion extends AbstractTaskBase {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  @Inject
  protected KubernetesCheckVolumeExpansion(
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

  protected KubernetesCheckVolumeExpansion.Params taskParams() {
    return (KubernetesCheckVolumeExpansion.Params) taskParams;
  }

  @Override
  public void run() {
    KubernetesManager k8s = kubernetesManagerFactory.getManager();
    Map<String, String> config = taskParams().config;
    if (config == null) {
      Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);
      config = CloudInfoInterface.fetchEnvVars(provider);
    }

    // storage class name used by tserver
    String scName =
        k8s.getStorageClassName(
            taskParams().config,
            taskParams().namespace,
            taskParams().helmReleaseName,
            false,
            taskParams().newNamingStyle);
    log.info("Verifiying that the PVC storage class {} allows volume expansion", scName);
    if (Strings.isNullOrEmpty(scName)) {
      // Could be using ephemeral volume
      throw new RuntimeException("TServer Volume does not support expansion");
    }
    boolean allowsExpansion = k8s.storageClassAllowsExpansion(taskParams().config, scName);
    if (!allowsExpansion) {
      throw new RuntimeException(
          String.format(
              "StorageClass %s should allow volume expansion for this operation", scName));
    }

    // verify there are no orphan PVCs
    List<PersistentVolumeClaim> pvcsInNs =
        k8s.getPVCs(
            taskParams().config,
            taskParams().namespace,
            taskParams().helmReleaseName,
            "yb-tserver",
            taskParams().newNamingStyle);
    Set<String> pvcNamesInNs =
        pvcsInNs.stream().map(pvc -> pvc.getMetadata().getName()).collect(Collectors.toSet());
    List<Pod> podsInNs =
        k8s.getPods(
            taskParams().config,
            taskParams().namespace,
            taskParams().helmReleaseName,
            "yb-tserver",
            taskParams().newNamingStyle);
    Set<String> pvcsAttachedToPods = new HashSet<>();
    podsInNs.stream()
        .map(
            pod -> {
              return pod.getSpec().getVolumes().stream()
                  .map(Volume::getPersistentVolumeClaim)
                  .filter(Objects::nonNull)
                  .map(PersistentVolumeClaimVolumeSource::getClaimName)
                  .collect(Collectors.toSet());
            })
        .forEach(pvcsAttachedToPods::addAll);
    log.info("Verifying that there are no orphan PVCs");
    log.info(
        "PVCs present in namespace {} should be same as attached PVCs {}",
        pvcNamesInNs,
        pvcsAttachedToPods);
    pvcNamesInNs.removeAll(pvcsAttachedToPods);
    if (!pvcNamesInNs.isEmpty()) {
      throw new RuntimeException(
          String.format(
              "Please remove these orphan PVCs from namespace %s"
                  + " before attempting this operation: %s",
              taskParams().namespace, pvcNamesInNs));
    }
  }
}
