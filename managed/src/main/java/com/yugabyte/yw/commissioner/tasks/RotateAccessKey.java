// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeAccessTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.RotateAccessKeyParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AddAuthorizedKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.RemoveAuthorizedKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseAccessKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.VerifyNodeSSHAccess;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.Collection;
import java.util.Objects;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class RotateAccessKey extends UniverseTaskBase {

  @Inject
  protected RotateAccessKey(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected RotateAccessKeyParams taskParams() {
    return (RotateAccessKeyParams) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    AccessKey newAccessKey = taskParams().newAccessKey;
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    checkPausedOrNonLiveNodes(universe, newAccessKey);
    for (Cluster cluster : universe.getUniverseDetails().clusters) {
      AccessKey clusterAccessKey =
          StringUtils.isBlank(cluster.userIntent.accessKeyCode)
              ? null
              : AccessKey.getOrBadRequest(
                  taskParams().providerUUID, cluster.userIntent.accessKeyCode);
      if (clusterAccessKey == null) {
        continue;
      }
      if (Objects.equals(
          clusterAccessKey.getPublicKeyContent(), newAccessKey.getPublicKeyContent())) {
        throw new RuntimeException(
            String.format(
                "New access key %s is the same as the current one for cluster %s in universe %s",
                newAccessKey.getKeyCode(), cluster.uuid, universe.getName()));
      }
    }
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = lockUniverse(-1);
    try {
      UUID customerUUID = taskParams().customerUUID;
      UUID universeUUID = taskParams().getUniverseUUID();
      UUID providerUUID = taskParams().providerUUID;
      AccessKey newAccessKey = taskParams().newAccessKey;
      Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
      String customSSHUser = Util.DEFAULT_YB_SSH_USER;
      UserTaskDetails.SubTaskGroupType subtaskGroupType =
          UserTaskDetails.SubTaskGroupType.RotateAccessKey;
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        AccessKey clusterAccessKey =
            StringUtils.isBlank(cluster.userIntent.accessKeyCode)
                ? null
                : AccessKey.getOrBadRequest(
                    providerUUID, cluster.userIntent.getAccessKeyCodeForProvider(providerUUID));
        String sudoSSHUser = provider.getDetails().sshUser;
        if (sudoSSHUser == null) {
          sudoSSHUser =
              provider.getDetails().sshUser != null
                  ? provider.getDetails().sshUser
                  : Util.DEFAULT_SUDO_SSH_USER;
        }
        Collection<NodeDetails> clusterNodes = universe.getNodesInCluster(cluster.uuid);
        // add key to yugabyte user.
        createNodeAccessTasks(
                clusterNodes,
                newAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                AddAuthorizedKey.class,
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // add key to sudo user.
        if (!provider.isManualOnprem()) {
          createNodeAccessTasks(
                  clusterNodes,
                  newAccessKey,
                  customerUUID,
                  providerUUID,
                  universeUUID,
                  AddAuthorizedKey.class,
                  sudoSSHUser)
              .setSubTaskGroupType(subtaskGroupType);
        }
        // verify connection to yugabyte user after adding the new key.
        createNodeAccessTasks(
                clusterNodes,
                newAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                VerifyNodeSSHAccess.class,
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // verify connection to sudo user after adding the new key.
        if (!provider.isManualOnprem()) {
          createNodeAccessTasks(
                  clusterNodes,
                  newAccessKey,
                  customerUUID,
                  providerUUID,
                  universeUUID,
                  VerifyNodeSSHAccess.class,
                  sudoSSHUser)
              .setSubTaskGroupType(subtaskGroupType);
        }
        if (clusterAccessKey != null) {
          // remove key from yugabte user
          createNodeAccessTasks(
                  clusterNodes,
                  clusterAccessKey,
                  customerUUID,
                  providerUUID,
                  universeUUID,
                  RemoveAuthorizedKey.class,
                  customSSHUser)
              .setSubTaskGroupType(subtaskGroupType);
          // remove key from sudo user
          if (!provider.isManualOnprem()) {
            createNodeAccessTasks(
                    clusterNodes,
                    clusterAccessKey,
                    customerUUID,
                    providerUUID,
                    universeUUID,
                    RemoveAuthorizedKey.class,
                    sudoSSHUser)
                .setSubTaskGroupType(subtaskGroupType);
          }
        }
        // verify connection to yugabyte user after removing the old key.
        createNodeAccessTasks(
                clusterNodes,
                newAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                VerifyNodeSSHAccess.class,
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // verify connection to sudo user after removing the old key.
        if (!provider.isManualOnprem()) {
          createNodeAccessTasks(
                  clusterNodes,
                  newAccessKey,
                  customerUUID,
                  providerUUID,
                  universeUUID,
                  VerifyNodeSSHAccess.class,
                  sudoSSHUser)
              .setSubTaskGroupType(subtaskGroupType);
        }
        createUpdateUniverseAccessKeyTask(
                universeUUID, providerUUID, cluster.uuid, newAccessKey.getKeyCode())
            .setSubTaskGroupType(subtaskGroupType);
      }
      getRunnableTask().runSubTasks();
      metricService.setOkStatusMetric(
          buildMetricTemplate(PlatformMetrics.SSH_KEY_ROTATION_STATUS, universe));
    } catch (Exception e) {
      log.error(
          "Access Key Rotation failed for universe: {} with uuid {}",
          universe.getName(),
          universe.getUniverseUUID());
      setSSHKeyRotationFailureMetric(universe);
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Successfully ran {}", getName());
  }

  public SubTaskGroup createNodeAccessTasks(
      Collection<NodeDetails> nodes,
      AccessKey accessKey,
      UUID customerUUID,
      UUID providerUUID,
      UUID universeUUID,
      Class<? extends ITask> taskClass,
      String sshUser) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup(taskClass.getSimpleName(), SubTaskGroupType.RotateAccessKey);
    for (NodeDetails node : nodes) {
      NodeAccessTaskParams params =
          new NodeAccessTaskParams(
              customerUUID, providerUUID, node.azUuid, universeUUID, accessKey, sshUser);
      params.regionUUID = params.getRegion().getUuid();
      params.nodeName = node.nodeName;
      NodeTaskBase task;
      if (taskClass == AddAuthorizedKey.class) {
        task = createTask(AddAuthorizedKey.class);
      } else if (taskClass == RemoveAuthorizedKey.class) {
        task = createTask(RemoveAuthorizedKey.class);
      } else if (taskClass == VerifyNodeSSHAccess.class) {
        task = createTask(VerifyNodeSSHAccess.class);
      } else {
        throw new RuntimeException("Invalid task class " + taskClass.getSimpleName());
      }
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateUniverseAccessKeyTask(
      UUID universeUUID, UUID providerUUID, UUID clusterUUID, String newAccessKeyCode) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup("UpdateUniverseAccessKey", SubTaskGroupType.RotateAccessKey);
    UpdateUniverseAccessKey.Params params = new UpdateUniverseAccessKey.Params();
    params.newAccessKeyCode = newAccessKeyCode;
    params.setUniverseUUID(universeUUID);
    params.clusterUUID = clusterUUID;
    params.providerUUID = providerUUID;
    UpdateUniverseAccessKey task = createTask(UpdateUniverseAccessKey.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  private void checkPausedOrNonLiveNodes(Universe universe, AccessKey newAccessKey) {
    if (universe.getUniverseDetails().universePaused) {
      setSSHKeyRotationFailureMetric(universe);
      throw new RuntimeException(
          String.format(
              "Universe %s is paused. Retry with access key %s after resuming it",
              universe.getName(), newAccessKey.getKeyCode()));
    }
    if (!universe.allNodesLive()) {
      // Throw Runtime Exception for non-live nodes when the nodes are actually down,
      // & not undergoing any other ops, during other ops nodes states can be
      // stopping/starting, etc.
      setSSHKeyRotationFailureMetric(universe);
      throw new RuntimeException(
          String.format(
              "Universe %s has non-live nodes. Retry with access key %s after fixing node status",
              universe.getName(), newAccessKey.getKeyCode()));
    }
  }

  private void setSSHKeyRotationFailureMetric(Universe universe) {
    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.SSH_KEY_ROTATION_STATUS, universe));
  }
}
