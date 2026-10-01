// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rollback.ResizeNodeRollbackComputer;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Function;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

/**
 * Rolls back a failed {@link ResizeNode} by reversing instance type / cgroup / gflags / IOPS /
 * throughput toward the {@code before} intent captured in {@code state_transition_details}. Never
 * decreases {@code volumeSize} - cloud disk grow is not reversible; the computer already picked
 * {@code max(before, current universe intent, cloud data-disk size)}.
 *
 * <p>Reuses {@link ResizeNode#run} which classifies nodes vs the params intent (now {@code
 * before}): nodes still at the failed target instance/cgroup become {@code instanceChangingNodes}
 * toward before; IOPS/throughput-only nodes take the non-restart {@code Disk_Update} path;
 * already-before nodes are skipped so rollback retry can make progress. Classification and the
 * ChangeInstanceType skip also consult the cloud so a modify that aborted before YBA persist is
 * still reverted.
 *
 * <p>Freeze must not recapture {@code state_transition_details} - the failed ResizeNode's delta is
 * the source of truth for the before intent. {@link
 * com.yugabyte.yw.commissioner.tasks.subtasks.PersistResizeNode} writes the overlaid intent (old
 * instance + kept volumeSize + reverted IOPS/throughput). Successful unlock clears {@code
 * state_transition_details} in {@link
 * com.yugabyte.yw.commissioner.tasks.UniverseTaskBase#unlockUniverseForUpdate(java.util.UUID,
 * String)}.
 */
@Abortable
@Retryable
@CanRollback(enabled = false)
public class RollbackResizeNode extends ResizeNode {

  private final ResizeNodeRollbackComputer rollbackComputer;
  private Map<String, CloudAPI.NodeDiskSpec> cloudByNode;

  @Inject
  protected RollbackResizeNode(
      BaseTaskDependencies baseTaskDependencies, ResizeNodeRollbackComputer rollbackComputer) {
    super(baseTaskDependencies);
    this.rollbackComputer = rollbackComputer;
  }

  /**
   * Preserve the failed {@link ResizeNode} delta. Re-capturing on freeze would diff the already
   * partially-resized universe against the rollback's {@code before} intent and lose the original
   * target snapshot.
   */
  @Override
  protected boolean shouldCaptureStateTransitionDelta() {
    return false;
  }

  /**
   * Skip forward ResizeNode's {@code verifyParams}, which would reject the reverse path with
   * "Nothing changed!" when some nodes already reached the failed target and could flag other
   * forward-only constraints. Generic universe checks still run via {@code super.validateParams}.
   * Rollback-eligibility is also re-checked in {@link #createPrecheckTasks}; feature-flag and
   * initial cooldown gates ran in the computer before submit.
   */
  @Override
  protected void verifyResizeParams(boolean isFirstTry) {
    Universe universe = getUniverse();
    if (ResizeNodeRollbackComputer.rejectKubernetes(universe)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Rollback of Kubernetes resize node is not supported");
    }
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back resize node: state_transition_details is missing (no delta was"
              + " captured on freeze)");
    }
    details.requireRollbackable();
  }

  /**
   * Re-runs eligibility and the shared disk-modify cooldown gate after lock/freeze. A retry can
   * still land inside the cloud window, and this is the last chance to fail before the rolling
   * instance-change subtasks fire. Skips {@code super.createPrecheckTasks} because that calls
   * {@code addBasicPrecheckTasks()} which assumes a healthy universe and would block rollback
   * exactly when it is needed (leaderless tablets after a partial resize, etc.).
   */
  @Override
  protected void createPrecheckTasks(Universe universe) {
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details == null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back resize node: state_transition_details is missing (no delta was"
              + " captured on freeze)");
    }
    details.requireRollbackable();
    Date failedTaskCreateTime = null;
    UUID originalTaskUUID = taskParams().getOriginalTaskUUID();
    if (originalTaskUUID != null) {
      Optional<TaskInfo> failedInfo = TaskInfo.maybeGet(originalTaskUUID);
      if (failedInfo.isPresent()) {
        failedTaskCreateTime = failedInfo.get().getCreateTime();
      }
    }
    cloudByNode = rollbackComputer.describeCloudNodes(universe);
    rollbackComputer.checkCooldownGate(taskParams(), universe, failedTaskCreateTime, cloudByNode);
  }

  @Override
  protected boolean isInstanceChanging(
      NodeDetails node,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      UniverseDefinitionTaskParams.UserIntent currentIntent) {
    if (super.isInstanceChanging(node, newIntent, currentIntent)) {
      return true;
    }
    CloudAPI.NodeDiskSpec cloud = cloudState(node);
    if (cloud == null || StringUtils.isBlank(cloud.getInstanceType())) {
      return false;
    }
    return !cloud.getInstanceType().equals(newIntent.getInstanceTypeForNode(node));
  }

  @Override
  protected boolean isModifyingDevice(
      NodeDetails node, DeviceInfo currentDeviceInfo, DeviceInfo newDeviceInfo) {
    return super.isModifyingDevice(node, currentDeviceInfo, newDeviceInfo)
        || cloudIopsOrThroughputDiffers(node, newDeviceInfo);
  }

  @Override
  protected String instanceTypeForChangeDecision(NodeDetails node) {
    CloudAPI.NodeDiskSpec cloud = cloudState(node);
    if (cloud != null && StringUtils.isNotBlank(cloud.getInstanceType())) {
      return cloud.getInstanceType();
    }
    return super.instanceTypeForChangeDecision(node);
  }

  @Override
  protected boolean isNodeAlreadyAtInstanceType(NodeDetails node, String targetInstanceType) {
    if (!super.isNodeAlreadyAtInstanceType(node, targetInstanceType)) {
      return false;
    }
    CloudAPI.NodeDiskSpec cloud = cloudState(node);
    if (cloud == null || StringUtils.isBlank(cloud.getInstanceType())) {
      return true;
    }
    return cloud.getInstanceType().equals(targetInstanceType);
  }

  @Override
  protected Map<UUID, Cluster> getGFlagsBaselineClusters(Universe universe) {
    UUID original = taskParams().getOriginalTaskUUID();
    if (original == null) {
      return super.getGFlagsBaselineClusters(universe);
    }
    Optional<TaskInfo> failedInfo = TaskInfo.maybeGet(original);
    if (!failedInfo.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back resize node gflags: original task is missing");
    }
    ResizeNodeParams forward =
        Json.fromJson(failedInfo.get().getTaskParams(), ResizeNodeParams.class);
    if (forward == null || forward.clusters == null || !forward.flagsProvided(universe)) {
      return super.getGFlagsBaselineClusters(universe);
    }
    return forward.getNewVersionsOfClusters(universe);
  }

  private boolean cloudIopsOrThroughputDiffers(NodeDetails node, DeviceInfo desired) {
    CloudAPI.NodeDiskSpec cloud = cloudState(node);
    if (cloud == null || desired == null) {
      return false;
    }
    return !ResizeNodeRollbackComputer.cloudAlreadyAtDesired(cloud, desired);
  }

  private CloudAPI.NodeDiskSpec cloudState(NodeDetails node) {
    if (cloudByNode == null) {
      Universe universe = getUniverse();
      cloudByNode = rollbackComputer.describeCloudNodes(universe);
    }
    CloudAPI.NodeDiskSpec cached = cloudByNode.get(node.nodeName);
    if (cached != null) {
      return cached;
    }
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(getUniverse());
    Provider provider = providerGetter.apply(node);
    if (provider == null
        || !ResizeNodeRollbackComputer.supportsCloudSnapshot(provider.getCloudCode())) {
      return null;
    }
    CloudAPI.NodeDiskSpec described = rollbackComputer.describeNodeCloudState(provider, node);
    if (cloudByNode == null) {
      cloudByNode = new HashMap<>();
    }
    cloudByNode.put(node.nodeName, described);
    return described;
  }
}
