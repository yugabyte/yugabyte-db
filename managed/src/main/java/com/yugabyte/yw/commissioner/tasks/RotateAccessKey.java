package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.NodeAccessTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.RotateAccessKeyParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AddAuthorizedKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.RemoveAuthorizedKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseAccessKey;
import com.yugabyte.yw.commissioner.tasks.subtasks.VerifyNodeSSHAccess;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.Collection;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RotateAccessKey extends UniverseTaskBase {

  @Inject NodeManager nodeManager;
  @Inject MetricService metricService;

  @Inject
  protected RotateAccessKey(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected RotateAccessKeyParams taskParams() {
    return (RotateAccessKeyParams) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    UUID customerUUID = taskParams().customerUUID;
    UUID universeUUID = taskParams().getUniverseUUID();
    UUID providerUUID = taskParams().providerUUID;
    AccessKey newAccessKey = taskParams().newAccessKey;
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    String customSSHUser = Util.DEFAULT_YB_SSH_USER;
    Universe universe = Universe.getOrBadRequest(universeUUID);
    checkPausedOrNonLiveNodes(universe, newAccessKey);
    try {
      lockUniverse(-1);
      UserTaskDetails.SubTaskGroupType subtaskGroupType =
          UserTaskDetails.SubTaskGroupType.RotateAccessKey;
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        AccessKey clusterAccessKey =
            AccessKey.getOrBadRequest(providerUUID, cluster.userIntent.accessKeyCode);
        String sudoSSHUser = provider.getDetails().sshUser;
        if (sudoSSHUser == null) {
          sudoSSHUser =
              provider.getDetails().sshUser != null
                  ? provider.getDetails().sshUser
                  : Util.DEFAULT_SUDO_SSH_USER;
        }
        Collection<NodeDetails> clusterNodes = universe.getNodesInCluster(cluster.uuid);
        // verify connection to yugabyte user
        createNodeAccessTasks(
                clusterNodes,
                clusterAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                newAccessKey,
                "VerifyNodeSSHAccess",
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // verify conenction to sudo user
        createNodeAccessTasks(
                clusterNodes,
                clusterAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                newAccessKey,
                "VerifyNodeSSHAccess",
                sudoSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // add key to yugabyte user
        createNodeAccessTasks(
                clusterNodes,
                clusterAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                newAccessKey,
                "AddAuthorizedKey",
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // add key to sudo user
        createNodeAccessTasks(
                clusterNodes,
                clusterAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                newAccessKey,
                "AddAuthorizedKey",
                sudoSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // remove key from sudo user
        createNodeAccessTasks(
                clusterNodes,
                newAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                clusterAccessKey,
                "RemoveAuthorizedKey",
                sudoSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        // remove key from yugabte user
        createNodeAccessTasks(
                clusterNodes,
                newAccessKey,
                customerUUID,
                providerUUID,
                universeUUID,
                clusterAccessKey,
                "RemoveAuthorizedKey",
                customSSHUser)
            .setSubTaskGroupType(subtaskGroupType);
        createUpdateUniverseAccessKeyTask(universeUUID, cluster.uuid, newAccessKey.getKeyCode())
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
      AccessKey taskAccessKey,
      String command,
      String sshUser) {
    SubTaskGroup subTaskGroup = createSubTaskGroup(command);
    for (NodeDetails node : nodes) {
      NodeAccessTaskParams params =
          new NodeAccessTaskParams(
              customerUUID, providerUUID, node.azUuid, universeUUID, accessKey, sshUser);
      params.regionUUID = params.getRegion().getUuid();
      params.nodeName = node.nodeName;
      params.taskAccessKey = taskAccessKey;
      NodeTaskBase task;
      if (command.equals("AddAuthorizedKey")) {
        task = createTask(AddAuthorizedKey.class);
      } else if (command.equals("RemoveAuthorizedKey")) {
        task = createTask(RemoveAuthorizedKey.class);
      } else {
        task = createTask(VerifyNodeSSHAccess.class);
      }
      task.initialize(params);
      task.setUserTaskUUID(getUserTaskUUID());
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createUpdateUniverseAccessKeyTask(
      UUID universeUUID, UUID clusterUUID, String newAccessKeyCode) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateUniverseAccessKey");

    UpdateUniverseAccessKey.Params params = new UpdateUniverseAccessKey.Params();
    params.newAccessKeyCode = newAccessKeyCode;
    params.setUniverseUUID(universeUUID);
    params.clusterUUID = clusterUUID;
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
          "The universe "
              + universe.getName()
              + " is paused,"
              + " cannot run access key rotation. Retry with access key "
              + newAccessKey.getKeyCode()
              + " after resuming it!");
    } else if (!universe.allNodesLive() && !universe.getUniverseDetails().updateInProgress) {
      // Throw Runtime Exception for non-live nodes when the nodes are actually down,
      // & not undergoing any other ops, during other ops nodes states can be
      // stopping/starting, etc.
      setSSHKeyRotationFailureMetric(universe);
      throw new RuntimeException(
          "The universe "
              + universe.getName()
              + " has non-live nodes,"
              + " cannot run access key rotation. Retry with access key "
              + newAccessKey.getKeyCode()
              + " after fixing node status!");
    }
  }

  private void setSSHKeyRotationFailureMetric(Universe universe) {
    metricService.setFailureStatusMetric(
        buildMetricTemplate(PlatformMetrics.SSH_KEY_ROTATION_STATUS, universe));
  }
}
