// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashSet;
import java.util.Optional;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet;
import org.yb.client.MasterErrorException;
import org.yb.client.XClusterDeleteOutboundReplicationGroupResponse;
import org.yb.client.YBClientApi;
import org.yb.util.NetUtil;

@Slf4j
public class DeleteReplicationOnSource extends XClusterConfigTaskBase {

  private final YbClientConfigFactory ybcClientConfigFactory;

  @Inject
  protected DeleteReplicationOnSource(
      BaseTaskDependencies baseTaskDependencies,
      RuntimeConfGetter confGetter,
      YbClientConfigFactory ybcClientConfigFactory,
      XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
    this.ybcClientConfigFactory = ybcClientConfigFactory;
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.
    // Whether the client RPC call ignore errors during replication deletion.
    public boolean ignoreErrors;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    long startTime = System.nanoTime();

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    YbClientConfig clientConfig;

    // When the parent task is FailoverDrConfig, we need to use a lower timeout for the client to
    // speed up the failover.
    Optional<TaskInfo> parentTaskOptional = TaskInfo.maybeGet(this.getUserTaskUUID());
    boolean isParentTaskSwitchover =
        parentTaskOptional
            .map(parentTask -> parentTask.getTaskType() == TaskType.SwitchoverDrConfig)
            .orElse(false);
    boolean isParentTaskFailover =
        parentTaskOptional
            .map(parentTask -> parentTask.getTaskType() == TaskType.FailoverDrConfig)
            .orElse(false);
    if (isParentTaskFailover) {
      long ybClientTimeoutMs =
          confGetter
              .getConfForScope(
                  sourceUniverse,
                  UniverseConfKeys.xclusterDbScopedDeleteReplicationOnSourceTimeoutDuringFailover)
              .toMillis();
      if (ybClientTimeoutMs == 0) {
        log.info("Skipping {}: the timeout is set to 0", getName());
        return;
      }
      log.debug(
          "Setting client timeout to {} ms because the parent task is failover", ybClientTimeoutMs);
      clientConfig =
          ybcClientConfigFactory.create(
              sourceUniverseMasterAddresses,
              sourceUniverseCertificate,
              ybClientTimeoutMs,
              ybClientTimeoutMs);
    } else {
      clientConfig =
          ybcClientConfigFactory.create(sourceUniverseMasterAddresses, sourceUniverseCertificate);
    }
    try (YBClientApi client = ybService.getClientWithConfig(clientConfig)) {
      Set<CommonNet.HostPortPB> targetMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  targetUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));
      try {
        XClusterDeleteOutboundReplicationGroupResponse response;
        try {
          response =
              client.xClusterDeleteOutboundReplicationGroup(
                  xClusterConfig.getReplicationGroupName(), targetMasterAddresses);
          if (response.hasError()) {
            throw new RuntimeException(response.errorMessage());
          }
        } catch (Exception e) {
          // During automatic DDL mode switchover, the DeleteOutboundReplicationGroup RPC must
          // succeed with the target master addresses so the OID on the new primary universe is
          // bumped. Older DB versions used by non-automatic mode do not support this argument.
          if (isParentTaskSwitchover && xClusterConfig.isAutomaticDdlMode()) {
            throw new RuntimeException(e);
          }
          log.warn(
              "Failed to delete outbound replication group {} with target master addresses. Error:"
                  + " {}. Retrying without target master addresses",
              xClusterConfig.getReplicationGroupName(),
              e.getMessage());
          response =
              client.xClusterDeleteOutboundReplicationGroup(
                  xClusterConfig.getReplicationGroupName());
        }
        if (response.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to delete replication for XClusterConfig(%s) on on source universe %s."
                      + " Error: %s",
                  xClusterConfig.getUuid(),
                  sourceUniverse.getUniverseUUID(),
                  response.errorMessage()));
        }
      } catch (MasterErrorException e) {
        // If it is not `NOT_FOUND` exception, rethrow the exception.
        if (!e.getMessage().contains("NOT_FOUND[code 1]")) {
          throw new RuntimeException(e);
        }
        log.warn(
            "Outbound replication group {} does not exist on the source universe, NOT_FOUND"
                + " exception occurred in xClusterDeleteOutboundReplicationGroup RPC call is"
                + " ignored: {}",
            xClusterConfig.getReplicationGroupName(),
            e.getMessage());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (!taskParams().ignoreErrors) {
        Throwables.propagate(e);
      }
      log.debug(
          "Ignoring failure of {} task as ignore error was set to {}",
          getName(),
          taskParams().ignoreErrors);
    }

    log.info("Completed (time: {}) {}", System.nanoTime() - startTime, getName());
  }
}
