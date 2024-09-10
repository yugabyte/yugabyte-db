// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentEnabler.NodeAgentInstaller;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallNodeAgent;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class NodeAgentInstallerImpl implements NodeAgentInstaller {
  private final RuntimeConfGetter confGetter;
  private final ImageBundleUtil imageBundleUtil;
  private final Commissioner commissioner;
  private final NodeAgentClient nodeAgentClient;

  @Inject
  protected NodeAgentInstallerImpl(
      RuntimeConfGetter confGetter,
      ImageBundleUtil imageBundleUtil,
      Commissioner commissioner,
      NodeAgentClient nodeAgentClient) {
    this.confGetter = confGetter;
    this.imageBundleUtil = imageBundleUtil;
    this.commissioner = commissioner;
    this.nodeAgentClient = nodeAgentClient;
  }

  @Override
  public boolean install(UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails) {
    if (isOnPremFullyManual(universeUuid, nodeDetails)) {
      // TODO Need to revisit this for on-prem installation once the user-level systemd changes are
      // done.
      return false;
    }
    InstallNodeAgent task = AbstractTaskBase.createTask(InstallNodeAgent.class);
    task.initialize(createInstallParams(customerUuid, universeUuid, nodeDetails, false));
    waitForNodeAgent(task.install());
    return true;
  }

  @Override
  public boolean reinstall(
      UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails, NodeAgent nodeAgent) {
    if (isOnPremFullyManual(universeUuid, nodeDetails)) {
      // TODO Need to revisit this for on-prem installation once the user-level systemd changes are
      // done.
      return false;
    }
    State state = nodeAgent.getState();
    if (state == State.REGISTERING) {
      InstallNodeAgent task = AbstractTaskBase.createTask(InstallNodeAgent.class);
      task.initialize(createInstallParams(customerUuid, universeUuid, nodeDetails, true));
      waitForNodeAgent(task.install());
    } else if (state == State.REGISTERED) {
      waitForNodeAgent(nodeAgent);
    }
    return true;
  }

  @Override
  public boolean migrate(UUID customerUuid, UUID universeUuid) {
    Optional<Universe> universeOpt = Universe.maybeGet(universeUuid);
    if (!universeOpt.isPresent()) {
      log.info("Universe {} is already destroyed", universeUuid);
      return false;
    }
    Universe universe = universeOpt.get();
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universeDetails.updateInProgress) {
      log.info(
          "Skipping migration because universe {} is already locked by another task {}({})",
          universeDetails.updatingTask,
          universeDetails.updatingTaskUUID);
      return false;
    }
    AllowedTasks allowedTasks =
        UniverseTaskBase.getAllowedTasksOnFailure(universeDetails.placementModificationTaskUuid);
    if (allowedTasks.isRestricted()
        && !allowedTasks.getTaskTypes().contains(TaskType.EnableNodeAgentInUniverse)) {
      log.info(
          "Skipping migration because universe {} is already frozen by another task {}({})",
          universeDetails.placementModificationTaskUuid,
          allowedTasks.getLockedTaskType());
      return false;
    }
    Customer customer = Customer.getOrBadRequest(customerUuid);
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universeUuid);
    UUID taskUuid = commissioner.submit(TaskType.EnableNodeAgentInUniverse, params);
    CustomerTask customerTask =
        CustomerTask.create(
            customer,
            universeUuid,
            taskUuid,
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.EnableNodeAgent,
            universeOpt.get().getName());
    commissioner.waitForTask(customerTask.getTaskUUID());
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUuid);
    return taskInfo.getTaskState() == TaskInfo.State.Success;
  }

  private InstallNodeAgent.Params createInstallParams(
      UUID customerUuid, UUID universeUuid, NodeDetails nodeDetails, boolean reinstall) {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    String installPath = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath);
    int serverPort = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentServerPort);
    InstallNodeAgent.Params params = new InstallNodeAgent.Params();
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    params.sshUser = imageBundleUtil.findEffectiveSshUser(provider, universe, nodeDetails);
    params.airgap = provider.getAirGapInstall();
    params.nodeName = nodeDetails.nodeName;
    params.customerUuid = customerUuid;
    params.azUuid = nodeDetails.azUuid;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeAgentInstallDir = installPath;
    params.nodeAgentPort = serverPort;
    params.reinstall = reinstall;
    if (StringUtils.isNotEmpty(nodeDetails.sshUserOverride)) {
      params.sshUser = nodeDetails.sshUserOverride;
    }
    return params;
  }

  private void waitForNodeAgent(NodeAgent nodeAgent) {
    nodeAgentClient.waitForServerReady(nodeAgent, Duration.ofMinutes(2));
    nodeAgent.saveState(State.READY);
  }

  private boolean isOnPremFullyManual(UUID universeUuid, NodeDetails nodeDetails) {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    if (cluster.userIntent.providerType != CloudType.onprem) {
      return false;
    }
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    return provider.getDetails().skipProvisioning;
  }
}
