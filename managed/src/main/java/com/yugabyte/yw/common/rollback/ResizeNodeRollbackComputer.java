// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.ResizeNodeParams.DiskResizeCooldownStatus;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Instant;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Function;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/**
 * Builds a {@link com.yugabyte.yw.commissioner.tasks.upgrade.RollbackResizeNode} submission for a
 * failed {@link TaskType#ResizeNode}. Only registered for {@code ResizeNode} in {@link
 * TaskRollbackModule}; Kubernetes disk resize rollback is out of scope.
 *
 * <p>Resize-node rollback is gated behind {@code yb.task.allow_resize_node_rollback}. Overlays the
 * failed task params with the pre-freeze intent captured in {@code state_transition_details} so
 * cluster userIntent (instance type, cgroup, gflags, IOPS, throughput) points back to {@code
 * before}, but {@code volumeSize} is never decreased - cloud disk grow is not reversible. Size is
 * {@code max(before, current universe intent, cloud data-disk size)}; the failed-task target is not
 * treated as applied.
 *
 * <p>Rejects at submit when a reverse {@code Disk_Update} for IOPS/throughput would hit the
 * provider and the cloud disk-modify cooldown is still active. The clock is the cloud modify time
 * when the provider records one (AWS/GCP). Azure has no such API, so it uses {@code
 * max(lastVolumeUpdateTime, failed ResizeNode create time)}. If the cloud disks already match the
 * rollback IOPS/throughput, cooldown is skipped (ybops no-ops that modify). Queries the cloud even
 * when YBA postgres already matches {@code before} (persist aborted after the cloud modify). The
 * same check runs again in {@link
 * com.yugabyte.yw.commissioner.tasks.upgrade.RollbackResizeNode#createPrecheckTasks}.
 */
@Singleton
@Slf4j
public class ResizeNodeRollbackComputer implements TaskRollbackComputer {

  public static final TaskType ROLLBACK_TASK_TYPE = TaskType.RollbackResizeNode;

  private final RuntimeConfGetter confGetter;
  private final CloudAPI.Factory cloudAPIFactory;

  @Inject
  public ResizeNodeRollbackComputer(
      RuntimeConfGetter confGetter, CloudAPI.Factory cloudAPIFactory) {
    this.confGetter = confGetter;
    this.cloudAPIFactory = cloudAPIFactory;
  }

  @Override
  public boolean isEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.allowResizeNodeRollback);
  }

  @Override
  public TaskType rollbackTaskType() {
    return ROLLBACK_TASK_TYPE;
  }

  @Override
  public boolean requiresStateTransitionDetails() {
    return true;
  }

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    TaskType taskType = context.getTaskInfo().getTaskType();
    // Second gate for direct API calls; listing already uses {@link #isEnabled()}.
    if (!isEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Rollback of %s tasks is not enabled. Set yb.task.allow_resize_node_rollback to"
                  + " enable it.",
              taskType));
    }
    ResizeNodeParams failedParams =
        Json.fromJson(context.getOldTaskParams(), ResizeNodeParams.class);
    Universe universe = Universe.getOrBadRequest(failedParams.getUniverseUUID());
    // Kubernetes disk resize rollback is not implemented; forward path uses
    // UpdateKubernetesDiskSize which reshapes StatefulSets and PVCs. Check every cluster
    // (including multi-cloud providerSpecifications), not just the primary providerType.
    if (rejectKubernetes(universe)) {
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

    UniverseDefinitionTaskParams beforeDetails = details.getBeforeUniverseDetails();
    Map<String, CloudAPI.NodeDiskSpec> cloudByNode = describeCloudNodes(universe);
    ResizeNodeParams rollbackParams =
        buildRollbackParams(failedParams, beforeDetails, universe, cloudByNode);
    // Azure cooldown starts at max(lastVolumeUpdateTime, failed task create time). Carried on the
    // params so the RollbackResizeNode precheck applies the same gate without the original task.
    rollbackParams.setFailedTaskCreateTime(context.getTaskInfo().getCreateTime());
    checkCooldownGate(
        rollbackParams, universe, rollbackParams.getFailedTaskCreateTime(), cloudByNode);

    // Skip optimistic version check for this programmatically-submitted task.
    rollbackParams.expectedUniverseVersion = -1;
    // Fresh task: must not inherit ResizeNode runtimeInfo / retry semantics.
    return new RollbackSubmission(
        rollbackTaskType(),
        rollbackParams,
        CustomerTask.TaskType.RollbackResizeNode,
        false /* setPreviousTaskUUID */);
  }

  /**
   * Overlay pre-freeze intent from {@code state_transition_details} on top of a copy of the failed
   * params. Reverts instance type, master instance type, cgroup, userIntent overrides, gflags /
   * {@code specificGFlags}, disk IOPS, and throughput. Keeps the larger {@code volumeSize} (never
   * decreased) computed as {@code max(before, current universe intent, cloud data-disk size)} per
   * cluster {@code deviceInfo}. The failed-task target is the intended size, not what the cloud
   * applied.
   */
  private ResizeNodeParams buildRollbackParams(
      ResizeNodeParams failedParams,
      UniverseDefinitionTaskParams before,
      Universe universe,
      Map<String, CloudAPI.NodeDiskSpec> cloudByNode) {
    // Start from a JSON round-trip of the failed params so we inherit clusters shape (uuid,
    // clusterType, placement) without carrying transient runtime fields.
    ResizeNodeParams rollbackParams =
        Json.fromJson(Json.toJson(failedParams), ResizeNodeParams.class);
    // Force is a forward "make it happen anyway" knob; it must not apply on the reverse path or
    // RollbackResizeNode would re-run ChangeInstanceType / Disk_Update on nodes already at before.
    rollbackParams.setForceResizeNode(false);
    // Roll top-level gflag maps back to before (used when specificGFlags is not present).
    Cluster beforePrimary = before.getPrimaryCluster();
    if (beforePrimary != null && beforePrimary.userIntent != null) {
      rollbackParams.masterGFlags = cloneMap(beforePrimary.userIntent.masterGFlags);
      rollbackParams.tserverGFlags = cloneMap(beforePrimary.userIntent.tserverGFlags);
    }

    UniverseDefinitionTaskParams current = universe.getUniverseDetails();
    for (Cluster rollbackCluster : rollbackParams.clusters) {
      Cluster beforeCluster = before.getClusterByUuid(rollbackCluster.uuid);
      Cluster currentCluster = current.getClusterByUuid(rollbackCluster.uuid);
      if (beforeCluster == null || beforeCluster.userIntent == null || currentCluster == null) {
        continue;
      }
      UserIntent beforeIntent = beforeCluster.userIntent;
      UserIntent currentIntent = currentCluster.userIntent;

      Integer cloudMaxTserverVolume =
          maxCloudVolumeSize(
              rollbackCluster.uuid,
              current.getNodesInCluster(rollbackCluster.uuid),
              cloudByNode,
              false /* master */);
      Integer cloudMaxMasterVolume =
          maxCloudVolumeSize(
              rollbackCluster.uuid,
              current.getNodesInCluster(rollbackCluster.uuid),
              cloudByNode,
              true /* master */);
      // Clone before so we can safely mutate volumeSize up to max(before, current, cloud).
      UserIntent restored = beforeIntent.clone();
      keepLargerVolumeSize(
          rollbackCluster.uuid,
          restored,
          beforeIntent,
          currentIntent,
          cloudMaxTserverVolume,
          cloudMaxMasterVolume);
      rollbackCluster.userIntent = restored;
    }
    return rollbackParams;
  }

  private static Map<String, String> cloneMap(Map<String, String> src) {
    return src == null ? null : new HashMap<>(src);
  }

  /**
   * For every {@code DeviceInfo} the rollback intent will apply to a node (top-level {@code
   * deviceInfo} and {@code masterDeviceInfo}), replace its {@code volumeSize} with {@code
   * max(before, current, cloud-for-that-role)}. Tserver and dedicated-master cloud sizes are kept
   * separate so a grow on one role cannot inflate the other. The failed-task target is ignored: it
   * is the requested size, and a failure before volume-size {@code Disk_Update} must not continue
   * the grow. IOPS and throughput are left at {@code before}.
   */
  private void keepLargerVolumeSize(
      UUID clusterUuid,
      UserIntent restored,
      UserIntent before,
      UserIntent current,
      Integer cloudTserverVolumeSize,
      Integer cloudMasterVolumeSize) {
    restored.deviceInfo =
        maxVolumeSizeDevice(
            clusterUuid,
            "tserver",
            restored.deviceInfo,
            before == null ? null : before.deviceInfo,
            current == null ? null : current.deviceInfo,
            cloudTserverVolumeSize);
    restored.masterDeviceInfo =
        maxVolumeSizeDevice(
            clusterUuid,
            "master",
            restored.masterDeviceInfo,
            before == null ? null : before.masterDeviceInfo,
            current == null ? null : current.masterDeviceInfo,
            cloudMasterVolumeSize);
  }

  private static DeviceInfo maxVolumeSizeDevice(
      UUID clusterUuid,
      String role,
      DeviceInfo restored,
      DeviceInfo before,
      DeviceInfo current,
      Integer cloudVolumeSize) {
    if (restored == null) {
      return null;
    }
    Integer beforeSize = before == null ? null : before.volumeSize;
    Integer currentSize = current == null ? null : current.volumeSize;
    Integer max = restored.volumeSize;
    max = maxNullable(max, beforeSize);
    max = maxNullable(max, currentSize);
    max = maxNullable(max, cloudVolumeSize);
    restored.volumeSize = max;
    log.info(
        "Selected rollback volumeSize {} for cluster {} {} (before={}, current={}, cloud={})",
        max,
        clusterUuid,
        role,
        beforeSize,
        currentSize,
        cloudVolumeSize);
    return restored;
  }

  private static Integer maxNullable(Integer a, Integer b) {
    if (a == null) {
      return b;
    }
    if (b == null) {
      return a;
    }
    return a > b ? a : b;
  }

  /**
   * Max cloud data-disk size for nodes of one role. {@code master=true} uses dedicated masters
   * ({@code dedicatedTo == MASTER}); {@code master=false} uses every other node (tservers and
   * non-dedicated clusters where {@code dedicatedTo} is null).
   */
  private static Integer maxCloudVolumeSize(
      UUID clusterUuid,
      Iterable<NodeDetails> nodes,
      Map<String, CloudAPI.NodeDiskSpec> cloudByNode,
      boolean master) {
    Integer max = null;
    for (NodeDetails node : nodes) {
      boolean isDedicatedMaster = node.dedicatedTo == ServerType.MASTER;
      if (master != isDedicatedMaster) {
        continue;
      }
      CloudAPI.NodeDiskSpec spec = cloudByNode.get(node.nodeName);
      if (spec != null) {
        max = maxNullable(max, spec.getVolumeSizeGb());
      }
    }
    log.info(
        "Rollback cloud max volumeSize {} for cluster {} {}",
        max,
        clusterUuid,
        master ? "master" : "tserver");
    return max;
  }

  /**
   * Reject when a reverse {@code Disk_Update} for IOPS/throughput would hit the provider and the
   * cloud disk-modify cooldown is still running. Shared with {@code RollbackResizeNode} precheck so
   * submit and retry cannot drift.
   *
   * <p>Queries the cloud on cooldown-capable clouds even when YBA already matches the rollback
   * intent (persist aborted after Disk_Update). If every data disk already has the desired
   * IOPS/throughput, cooldown is skipped (ybops no-ops the modify). Otherwise AWS/GCP clock from
   * the cloud modification start. AWS with no modification history uses {@link Instant#EPOCH} (the
   * cooldown has already expired). A missing start time fails closed on every cloud. Azure has no
   * last-resize API, so it clocks {@code max(lastVolumeUpdateTime, failed ResizeNode create time)};
   * both null fails closed. A describe error fails closed and is never treated as "no cooldown".
   *
   * <p>Operator escape hatch: {@code yb.task.skip_resize_node_rollback_cooldown} skips the query.
   * The cloud may still reject the reverse Disk_Update if the window is truly active.
   */
  public void checkCooldownGate(
      ResizeNodeParams rollbackParams, Universe universe, Date failedTaskCreateTime) {
    checkCooldownGate(rollbackParams, universe, failedTaskCreateTime, null);
  }

  public void checkCooldownGate(
      ResizeNodeParams rollbackParams,
      Universe universe,
      Date failedTaskCreateTime,
      Map<String, CloudAPI.NodeDiskSpec> cloudByNode) {
    if (confGetter.getGlobalConf(GlobalConfKeys.skipResizeNodeRollbackCooldown)) {
      log.warn(
          "Skipping resize-node rollback cooldown gate for universe {} because"
              + " yb.task.skip_resize_node_rollback_cooldown is enabled",
          universe.getUniverseUUID());
      return;
    }
    UniverseDefinitionTaskParams current = universe.getUniverseDetails();
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(universe);
    for (Cluster rollbackCluster : rollbackParams.clusters) {
      Cluster currentCluster = current.getClusterByUuid(rollbackCluster.uuid);
      if (currentCluster == null) {
        continue;
      }
      UserIntent currentIntent = currentCluster.userIntent;
      UserIntent rollbackIntent = rollbackCluster.userIntent;
      for (NodeDetails node : current.getNodesInCluster(rollbackCluster.uuid)) {
        DeviceInfo curDev = currentIntent.evaluateDeviceInfoForNode(node);
        DeviceInfo newDev = rollbackIntent.evaluateDeviceInfoForNode(node);
        Provider provider = providerGetter.apply(node);
        if (provider == null) {
          continue;
        }
        Common.CloudType cloud = provider.getCloudCode();
        if (!supportsCloudSnapshot(cloud)) {
          continue;
        }
        // Storage type is stable across resize; prefer rollback intent, fall back to current when
        // before lacked the cooldown-capable type (e.g. tests that flip storage after the delta).
        DeviceInfo cooldownDev = newDev;
        if (!cloudHasDiskResizeCooldown(cloud, cooldownDev)) {
          cooldownDev = curDev;
        }
        if (!cloudHasDiskResizeCooldown(cloud, cooldownDev)) {
          continue;
        }
        CloudAPI.NodeDiskSpec perf = resolveCloudState(provider, node, cloudByNode);
        if (cloudAlreadyAtDesired(perf, newDev)) {
          log.info(
              "Skipping disk-modify cooldown for node {}: cloud IOPS/throughput already match"
                  + " rollback intent",
              node.nodeName);
          continue;
        }
        Date start = cooldownStart(cloud, perf, node, failedTaskCreateTime);
        DiskResizeCooldownStatus status =
            ResizeNodeParams.evaluateDiskResizeCooldown(
                cloud, cooldownDev, start, null, confGetter);
        if (status != null && status.isActive()) {
          throw new PlatformServiceException(BAD_REQUEST, status.getMessage());
        }
      }
    }
  }

  /**
   * Describe instance + data disks for every node on AWS/GCP/Azure. Empty Optional on those clouds
   * fails closed. Onprem and other clouds are omitted (YBA-only classification).
   */
  public Map<String, CloudAPI.NodeDiskSpec> describeCloudNodes(Universe universe) {
    Map<String, CloudAPI.NodeDiskSpec> result = new HashMap<>();
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(universe);
    for (NodeDetails node : universe.getNodes()) {
      Provider provider = providerGetter.apply(node);
      if (provider == null || !supportsCloudSnapshot(provider.getCloudCode())) {
        continue;
      }
      result.put(node.nodeName, describeNodeCloudState(provider, node));
    }
    return result;
  }

  /**
   * Public describe used by {@link com.yugabyte.yw.commissioner.tasks.upgrade.RollbackResizeNode}.
   */
  public CloudAPI.NodeDiskSpec describeNodeCloudState(Provider provider, NodeDetails node) {
    CloudAPI cloudAPI = cloudAPIFactory.get(provider.getCode());
    if (cloudAPI == null) {
      throw couldNotVerify(node, "no cloud API for " + provider.getCode());
    }
    Optional<CloudAPI.NodeDiskSpec> perf;
    try {
      perf = cloudAPI.describeNodeDataDiskSpec(provider, node);
    } catch (RuntimeException e) {
      log.warn("Failed to describe cloud state for node {}", node.nodeName, e);
      String reason = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
      throw couldNotVerify(node, reason);
    }
    if (perf == null || perf.isEmpty()) {
      throw couldNotVerify(node, "cloud returned no node snapshot");
    }
    return perf.get();
  }

  public static boolean supportsCloudSnapshot(Common.CloudType cloud) {
    return cloud == Common.CloudType.aws
        || cloud == Common.CloudType.gcp
        || cloud == Common.CloudType.azu;
  }

  /**
   * {@code evaluateDiskResizeCooldown} returns null when this cloud/storage type has no window. A
   * long-expired start still yields a status, so epoch distinguishes "has a window" from "no
   * cooldown" without consulting YBA timestamps.
   */
  private boolean cloudHasDiskResizeCooldown(Common.CloudType cloud, DeviceInfo device) {
    return ResizeNodeParams.evaluateDiskResizeCooldown(cloud, device, new Date(0), null, confGetter)
        != null;
  }

  private CloudAPI.NodeDiskSpec resolveCloudState(
      Provider provider, NodeDetails node, Map<String, CloudAPI.NodeDiskSpec> cloudByNode) {
    if (cloudByNode != null && cloudByNode.containsKey(node.nodeName)) {
      return cloudByNode.get(node.nodeName);
    }
    return describeNodeCloudState(provider, node);
  }

  /**
   * Cooldown start after a modify would actually run. A missing start time fails closed. Azure
   * never uses a cloud start time. AWS never-modified volumes should arrive as {@link
   * Instant#EPOCH} from describe.
   */
  private static Date cooldownStart(
      Common.CloudType cloud,
      CloudAPI.NodeDiskSpec spec,
      NodeDetails node,
      Date failedTaskCreateTime) {
    if (cloud == Common.CloudType.azu) {
      Date start = maxDate(node.lastVolumeUpdateTime, failedTaskCreateTime);
      if (start == null) {
        throw couldNotVerify(node, "Azure disk cooldown clocks are missing");
      }
      return start;
    }
    Instant lastModificationStart = spec.getLastModificationStart();
    if (lastModificationStart == null) {
      throw couldNotVerify(node, "cloud disk last-modification time is missing");
    }
    return Date.from(lastModificationStart);
  }

  /**
   * True when any cluster uses Kubernetes, including a multi-cloud {@code providerSpecifications}
   * entry. Kubernetes disk resize rollback is not implemented.
   */
  public static boolean rejectKubernetes(Universe universe) {
    return universe.getUniverseDetails().clusters.stream()
        .anyMatch(
            c ->
                c.userIntent != null
                    && Util.checkAnyProviderType(
                        c.userIntent, t -> t == Common.CloudType.kubernetes));
  }

  /** Null desired field is ignored (same as ybops treating an omitted IOPS/throughput as no-op). */
  public static boolean cloudAlreadyAtDesired(CloudAPI.NodeDiskSpec perf, DeviceInfo desired) {
    if (desired == null) {
      return false;
    }
    return fieldMatches(perf.getDiskIops(), desired.diskIops)
        && fieldMatches(perf.getThroughput(), desired.throughput);
  }

  private static boolean fieldMatches(Integer cloud, Integer desired) {
    return desired == null || Objects.equals(cloud, desired);
  }

  private static Date maxDate(Date a, Date b) {
    if (a == null) {
      return b;
    }
    if (b == null) {
      return a;
    }
    return a.after(b) ? a : b;
  }

  private static PlatformServiceException couldNotVerify(NodeDetails node, String reason) {
    return new PlatformServiceException(
        BAD_REQUEST,
        String.format(
            "Could not verify cloud disk cooldown for node %s: %s",
            node == null || node.nodeName == null ? "unknown" : node.nodeName, reason));
  }

  /**
   * Reverting the volume size never fires a Disk_Update on the reverse path (we keep the larger
   * size), so this only tracks IOPS/throughput. Used by tests and callers that compare YBA intents;
   * the cooldown gate itself uses cloud vs desired.
   */
  public static boolean wouldChangeIopsOrThroughput(DeviceInfo current, DeviceInfo target) {
    if (current == null || target == null) {
      return false;
    }
    return !Objects.equals(current.diskIops, target.diskIops)
        || !Objects.equals(current.throughput, target.throughput);
  }
}
