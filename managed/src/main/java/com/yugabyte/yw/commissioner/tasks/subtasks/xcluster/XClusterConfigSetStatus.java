// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigSetStatus extends XClusterConfigTaskBase {

  // It contains the transitions from current status to a list of target statuses that happen
  // when a user wants to pause/enable a running xCluster config.
  private static final Map<XClusterConfigStatusType, List<XClusterConfigStatusType>>
      enableAndDisableTransitions = new HashMap<>();

  static {
    enableAndDisableTransitions.put(
        XClusterConfigStatusType.Running, Arrays.asList(XClusterConfigStatusType.Paused));
    enableAndDisableTransitions.put(
        XClusterConfigStatusType.Paused, Arrays.asList(XClusterConfigStatusType.Running));
  }

  @Inject
  protected XClusterConfigSetStatus(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // The status of the xCluster config before changing it to `Updating`.
    public XClusterConfigStatusType currentStatus;
    // The desired status to put the xCluster config in.
    public XClusterConfigStatusType desiredStatus;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s %s(currentStatus=%s, desiredStatus=%s)",
        super.getName(),
        this.getClass().getSimpleName(),
        taskParams().xClusterConfig.status,
        taskParams().desiredStatus);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // Each set status task must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = taskParams().xClusterConfig;
    if (xClusterConfig == null) {
      throw new RuntimeException(
          "taskParams().xClusterConfig is null. Each set status subtask must belong to an "
              + "xCluster config");
    }

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {

      // Handle pausing/enabling a running xCluster config.
      if (isEnableAndDisableTransition(taskParams().currentStatus, taskParams().desiredStatus)) {
        SetUniverseReplicationEnabledResponse resp =
            client.setUniverseReplicationEnabled(
                xClusterConfig.getReplicationGroupName(),
                taskParams().desiredStatus == XClusterConfigStatusType.Running /* active */);
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to set XClusterConfig(%s) status: %s",
                  xClusterConfig.uuid, resp.errorMessage()));
        }

        if (HighAvailabilityConfig.get().isPresent()) {
          getUniverse(true).incrementVersion();
        }
      }

      // Save the desired status in the DB.
      xClusterConfig.status = taskParams().desiredStatus;
      xClusterConfig.update();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private static boolean isEnableAndDisableTransition(
      XClusterConfigStatusType currentStatus, XClusterConfigStatusType desiredStatus) {
    if (currentStatus == null) {
      return false;
    }
    List<XClusterConfigStatusType> destinationStatusList =
        enableAndDisableTransitions.get(currentStatus);
    if (destinationStatusList == null) {
      return false;
    }
    return destinationStatusList.contains(desiredStatus);
  }
}
