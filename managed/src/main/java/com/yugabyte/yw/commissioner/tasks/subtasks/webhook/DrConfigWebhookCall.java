// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.webhook;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.forms.webhook.WebhookTaskParams;
import com.yugabyte.yw.forms.webhook.XClusterWebhookRequestBody;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DrConfigWebhookCall extends WebhookTaskBase {

  @Inject
  protected DrConfigWebhookCall(
      WSClientRefresher wsClientRefresher, BaseTaskDependencies baseTaskDependencies) {
    super(wsClientRefresher, baseTaskDependencies);
  }

  public static class Params extends WebhookTaskParams {
    public UUID drConfigUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = DrConfig.getOrBadRequest(taskParams().drConfigUuid);
    XClusterConfig activeXClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse =
        Universe.getOrBadRequest(activeXClusterConfig.getSourceUniverseUUID());
    List<NodeDetails> tserverNodes = sourceUniverse.getTServers();
    String nodeIpCsv =
        tserverNodes.stream().map(nd -> nd.cloudInfo.private_ip).collect(Collectors.joining(","));

    log.debug(
        "Sending webhook for dr config: {}, xcluster config: {}, primary universe: {}",
        drConfig.getUuid(),
        activeXClusterConfig.getUuid(),
        sourceUniverse.getName());
    XClusterWebhookRequestBody requestBody = new XClusterWebhookRequestBody();
    requestBody.setDrConfigUuid(taskParams().drConfigUuid);
    requestBody.setIps(nodeIpCsv);
    requestBody.setRecordType("A");
    requestBody.setTtl(5);

    // We do not want to fail failover if webhook call fails.
    try {
      send(requestBody);
    } catch (Exception e) {
      log.error("{} hit error :", getName(), e);
    }

    log.info("Completed {}", getName());
  }
}
