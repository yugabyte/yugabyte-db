// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet;
import org.yb.client.CreateXClusterReplicationResponse;
import org.yb.client.IsCreateXClusterReplicationDoneResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class XClusterDbReplicationSetup extends XClusterConfigTaskBase {
  private static long DELAY_BETWEEN_RETRIES_MS = TimeUnit.SECONDS.toMillis(10);

  @Inject
  protected XClusterDbReplicationSetup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  protected XClusterConfigTaskParams taskParams() {
    return (XClusterConfigTaskParams) taskParams;
  }

  @Override
  public void run() {
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    Duration xclusterWaitTimeout =
        this.confGetter.getConfForScope(sourceUniverse, UniverseConfKeys.xclusterSetupAlterTimeout);

    try (YBClient client =
        ybService.getClient(
            sourceUniverse.getMasterAddresses(), sourceUniverse.getCertificateNodetoNode())) {

      Set<CommonNet.HostPortPB> targetMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  targetUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));
      CreateXClusterReplicationResponse createResponse =
          client.createXClusterReplication(xClusterConfig.getName(), targetMasterAddresses);
      if (createResponse.hasError()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "CreateXClusterReplicationResponse rpc failed for xClusterConfig, %s, with error"
                    + " message: %s",
                xClusterConfig.getUuid(), createResponse.errorMessage()));
      }

      boolean setupDone =
          doWithConstTimeout(
              DELAY_BETWEEN_RETRIES_MS,
              xclusterWaitTimeout.toMillis(),
              () -> {
                IsCreateXClusterReplicationDoneResponse doneResponse;
                try {
                  doneResponse =
                      client.isCreateXClusterReplicationDone(
                          xClusterConfig.getName(), targetMasterAddresses);
                } catch (Exception e) {
                  log.error(
                      "IsCreateXClusterReplicationDone rpc for xClusterConfig: {}, hit error: {}",
                      xClusterConfig.getUuid(),
                      e);
                  return false;
                }

                if (doneResponse.hasError()) {
                  log.error(
                      "IsCreateXClusterReplicationDone rpc for xClusterConfig: {}, hit error: {}",
                      xClusterConfig.getUuid(),
                      doneResponse.errorMessage());
                  return false;
                }
                if (doneResponse.hasReplicationError()) {
                  log.error(
                      "IsCreateXClusterReplicationDone rpc for xClusterConfig: {}, hit replication"
                          + " error: {} with error code: {}",
                      xClusterConfig.getUuid(),
                      doneResponse.getReplicationError().getMessage(),
                      doneResponse.getReplicationError().getCode());
                  return false;
                }
                return true;
              });

      if (!setupDone) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Setup xcluster replication xClusterConfig %s timed out",
                xClusterConfig.getName()));
      }

      log.debug(
          "XCluster db replication setup complete for xClusterConfig: {}",
          xClusterConfig.getUuid());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }
}
