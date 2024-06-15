/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class CreateKubernetesUniverse extends KubernetesTaskBase {

  // In-memory password store for ysqlPassword and ycqlPassword.
  private static final Cache<UUID, AuthPasswords> passwordStore =
      CacheBuilder.newBuilder().expireAfterAccess(2, TimeUnit.DAYS).maximumSize(1000).build();

  @AllArgsConstructor
  private static class AuthPasswords {
    public String ycqlPassword;
    public String ysqlPassword;
  }

  private final OperatorStatusUpdater kubernetesStatus;

  @Inject
  protected CreateKubernetesUniverse(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies);
    this.kubernetesStatus = operatorStatusUpdaterFactory.create();
  }

  @Override
  public void run() {
    Throwable th = null;
    try {
      if (isFirstTry()) {
        // Verify the task params.
        verifyParams(UniverseOpType.CREATE);
      }
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      boolean cacheYCQLAuthPass =
          primaryCluster.userIntent.enableYCQL
              && primaryCluster.userIntent.enableYCQLAuth
              && !primaryCluster.userIntent.defaultYcqlPassword;
      boolean cacheYSQLAuthPass =
          primaryCluster.userIntent.enableYSQL
              && primaryCluster.userIntent.enableYSQLAuth
              && !primaryCluster.userIntent.defaultYsqlPassword;
      if (cacheYCQLAuthPass || cacheYSQLAuthPass) {
        if (isFirstTry()) {
          if (cacheYSQLAuthPass) {
            ysqlPassword = primaryCluster.userIntent.ysqlPassword;
          }
          if (cacheYCQLAuthPass) {
            ycqlPassword = primaryCluster.userIntent.ycqlPassword;
          }
          passwordStore.put(
              taskParams().getUniverseUUID(), new AuthPasswords(ycqlPassword, ysqlPassword));
        } else {
          log.debug("Reading password for {}", taskParams().getUniverseUUID());
          // Read from the in-memory store on retry.
          AuthPasswords passwords = passwordStore.getIfPresent(taskParams().getUniverseUUID());
          if (passwords == null) {
            throw new RuntimeException(
                "Auth passwords are not found. Platform might have restarted"
                    + " or task might have expired");
          }
          ycqlPassword = passwords.ycqlPassword;
          ysqlPassword = passwords.ysqlPassword;
        }
      }

      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);
      kubernetesStatus.startYBUniverseEventStatus(
          universe,
          taskParams().getKubernetesResourceDetails(),
          TaskType.CreateKubernetesUniverse.name(),
          getUserTaskUUID(),
          UniverseState.CREATING);

      // Set all the in-memory node names first.
      setNodeNames(universe);

      PlacementInfo pi = primaryCluster.placementInfo;

      selectNumMastersAZ(pi);

      // Update the user intent.
      writeUserIntentToUniverse();

      Provider provider =
          Provider.getOrBadRequest(
              UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));

      KubernetesPlacement placement = new KubernetesPlacement(pi, /*isReadOnlyCluster*/ false);

      boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

      String masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              pi,
              placement.masters,
              taskParams().nodePrefix,
              universe.getName(),
              provider,
              taskParams().communicationPorts.masterRpcPort,
              newNamingStyle);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

      createPodsTask(
          universe.getName(),
          placement,
          masterAddresses,
          false, /*isReadOnlyCluster*/
          taskParams().isEnableYbc());

      createSingleKubernetesExecutorTask(
          universe.getName(),
          KubernetesCommandExecutor.CommandType.POD_INFO,
          pi,
          false); /*isReadOnlyCluster*/

      installThirdPartyPackagesTaskK8s(
          universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS);
      Set<NodeDetails> tserversAdded =
          getPodsToAdd(placement.tservers, null, ServerType.TSERVER, isMultiAz, false);

      // Check if we need to create read cluster pods also.
      List<Cluster> readClusters = taskParams().getReadOnlyClusters();
      Set<NodeDetails> readOnlyTserversAdded = new HashSet<>();
      if (readClusters.size() > 1) {
        String msg = "Expected at most 1 read cluster but found " + readClusters.size();
        log.error(msg);
        throw new RuntimeException(msg);
      } else if (readClusters.size() == 1) {
        Cluster readCluster = readClusters.get(0);
        PlacementInfo readClusterPI = readCluster.placementInfo;
        Provider readClusterProvider =
            Provider.getOrBadRequest(UUID.fromString(readCluster.userIntent.provider));
        CloudType readClusterProviderType = readCluster.userIntent.providerType;
        if (readClusterProviderType != CloudType.kubernetes) {
          String msg =
              String.format(
                  "Read replica clusters provider type is expected to be kubernetes but found %s",
                  readClusterProviderType.name());
          log.error(msg);
          throw new IllegalArgumentException(msg);
        }

        KubernetesPlacement readClusterPlacement =
            new KubernetesPlacement(readClusterPI, /*isReadOnlyCluster*/ true);
        // Skip choosing masters from read cluster.
        boolean isReadClusterMultiAz = PlacementInfoUtil.isMultiAZ(readClusterProvider);
        createPodsTask(
            universe.getName(),
            readClusterPlacement,
            masterAddresses,
            true,
            taskParams().isEnableYbc());
        createSingleKubernetesExecutorTask(
            universe.getName(),
            KubernetesCommandExecutor.CommandType.POD_INFO,
            readClusterPI,
            true);
        readOnlyTserversAdded =
            getPodsToAdd(
                readClusterPlacement.tservers,
                null,
                ServerType.TSERVER,
                isReadClusterMultiAz,
                true);
      }

      // Wait for new tablet servers to be responsive.
      Set<NodeDetails> allTserversAdded = new HashSet<>();
      allTserversAdded.addAll(tserversAdded);
      allTserversAdded.addAll(readOnlyTserversAdded);
      createWaitForServersTasks(allTserversAdded, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Install YBC on the pods
      if (taskParams().isEnableYbc()) {
        installYbcOnThePods(
            universe.getName(),
            tserversAdded,
            false,
            taskParams().getYbcSoftwareVersion(),
            taskParams().getPrimaryCluster().userIntent.ybcFlags);
        if (readClusters.size() == 1) {
          installYbcOnThePods(
              universe.getName(),
              readOnlyTserversAdded,
              true,
              taskParams().getYbcSoftwareVersion(),
              taskParams().getReadOnlyClusters().get(0).userIntent.ybcFlags);
        }
        createWaitForYbcServerTask(allTserversAdded);
        createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      createConfigureUniverseTasks(primaryCluster, null);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      th = t;
      throw t;
    } finally {
      kubernetesStatus.updateYBUniverseStatus(
          getUniverse(),
          taskParams().getKubernetesResourceDetails(),
          TaskType.CreateKubernetesUniverse.name(),
          getUserTaskUUID(),
          (th != null) ? UniverseState.ERROR_CREATING : UniverseState.READY,
          th);
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
