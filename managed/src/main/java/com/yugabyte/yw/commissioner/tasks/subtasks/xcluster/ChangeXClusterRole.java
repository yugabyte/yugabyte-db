package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import java.util.Objects;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.client.ChangeXClusterRoleResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;

@Slf4j
public class ChangeXClusterRole extends XClusterConfigTaskBase {

  @Inject
  protected ChangeXClusterRole(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.

    // The source universe role.
    public XClusterRole sourceRole;

    // The target universe role.
    public XClusterRole targetRole;

    // Whether to ignore errors that happened during this subtask execution.
    public boolean ignoreErrors;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,sourceRole=%s,targetRole=%s,ignoreErrors=%s)",
        super.getName(),
        taskParams().getXClusterConfig(),
        taskParams().sourceRole,
        taskParams().targetRole,
        taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe universe = null;
    XClusterRole requestedRole = null;

    if (!xClusterConfig.getType().equals(ConfigType.Txn)) {
      throw new IllegalArgumentException(
          "XCluster role can change only for transactional xCluster configs");
    }

    if (Objects.nonNull(taskParams().sourceRole) && Objects.nonNull(taskParams().targetRole)) {
      throw new IllegalArgumentException("The role of only one universe can be set");
    }

    if (Objects.nonNull(taskParams().sourceRole)) {
      if (Objects.isNull(xClusterConfig.getSourceUniverseUUID())) {
        log.warn("Skipped {}: the source universe is destroyed", getName());
        return;
      }
      universe = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
      requestedRole = taskParams().sourceRole;
    }
    if (Objects.nonNull(taskParams().targetRole)) {
      if (Objects.isNull(xClusterConfig.getTargetUniverseUUID())) {
        log.warn("Skipped {}: the target universe is destroyed", getName());
        return;
      }
      universe = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
      requestedRole = taskParams().targetRole;
    }
    if (Objects.isNull(universe) || Objects.isNull(requestedRole)) {
      throw new IllegalArgumentException("No role change is requested");
    }

    String universeMasterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    try (YBClient client = ybService.getClient(universeMasterAddresses, universeCertificate)) {
      // Sync roles in YBA with YBDB.
      GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
      XClusterRole currentXClusterRole =
          clusterConfigResp.getConfig().getConsumerRegistry().getDEPRECATEDRole();
      if (Objects.nonNull(taskParams().sourceRole)) {
        xClusterConfig.setSourceActive(currentXClusterRole == XClusterRole.ACTIVE);
      } else {
        xClusterConfig.setTargetActive(currentXClusterRole == XClusterRole.ACTIVE);
      }
      xClusterConfig.update();
      log.info(
          "Current universe role for universe {} is {}",
          universe.getUniverseUUID(),
          currentXClusterRole);

      if (Objects.equals(currentXClusterRole, requestedRole)) {
        log.info("Skipped {}: requested role is the same as currentXClusterRole", getName());
        return;
      }

      ChangeXClusterRoleResponse resp = client.changeXClusterRole(requestedRole);

      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to set the role for universe %s to %s on XClusterConfig(%s): %s",
                universe.getUniverseUUID(), requestedRole, xClusterConfig, resp.errorMessage()));
      }
      log.info(
          "Universe role for universe {} was set to {}", universe.getUniverseUUID(), requestedRole);

      if (HighAvailabilityConfig.get().isPresent()) {
        universe.incrementVersion();
      }

      // Save the role in the DB.
      if (Objects.nonNull(taskParams().sourceRole)) {
        xClusterConfig.setSourceActive(requestedRole == XClusterRole.ACTIVE);
      } else {
        xClusterConfig.setTargetActive(requestedRole == XClusterRole.ACTIVE);
      }
      xClusterConfig.update();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (!taskParams().ignoreErrors) {
        throw new RuntimeException(e);
      }
      log.warn("Ignoring the error: {}", e.getMessage());
    }

    log.info("Completed {}", getName());
  }
}
