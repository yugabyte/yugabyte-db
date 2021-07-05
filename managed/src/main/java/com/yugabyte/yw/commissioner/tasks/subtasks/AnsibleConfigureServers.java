/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;

import javax.inject.Inject;
import java.util.*;
import java.util.stream.Collectors;

@Slf4j
public class AnsibleConfigureServers extends NodeTaskBase {

  @Inject
  protected AnsibleConfigureServers(
      BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends NodeTaskParams {
    public UpgradeUniverse.UpgradeTaskType type = UpgradeUniverse.UpgradeTaskType.Everything;
    public String ybSoftwareVersion = null;

    // Optional params.
    public boolean isMasterInShellMode = false;
    public boolean isMaster = false;
    public boolean enableYSQL = false;
    public boolean enableYEDIS = false;
    public Map<String, String> gflags = new HashMap<>();
    public Set<String> gflagsToRemove = new HashSet<>();
    public boolean updateMasterAddrsOnly = false;
    public CollectionLevel callhomeLevel;
    // Development params.
    public String itestS3PackagePath = "";
    // ToggleTls params.
    public boolean enableNodeToNodeEncrypt = false;
    public boolean enableClientToNodeEncrypt = false;
    public boolean allowInsecure = true;
    // 0 => No change in node-to-node encryption
    // > 0 => node-to-node encryption is enabled
    // < 0 => node-to-node encryption is disabled
    public int nodeToNodeChange = 0;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    // Execute the ansible command.
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Configure, taskParams());
    processShellResponse(response);

    if (taskParams().type == UpgradeUniverse.UpgradeTaskType.Everything
        && !taskParams().updateMasterAddrsOnly) {
      // Check cronjob status if installing software.
      response = getNodeManager().nodeCommand(NodeManager.NodeCommandType.CronCheck, taskParams());

      // Create an alert if the cronjobs failed to be created on this node.
      if (response.code != 0) {
        Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
        Customer cust = Customer.get(universe.customerId);
        String nodeName = taskParams().nodeName;
        String alertMsg =
            "Universe %s was successfully created but failed to "
                + "create cronjobs on some nodes (%s)";

        // Persist node cronjob status into the DB.
        UniverseUpdater updater =
            new UniverseUpdater() {
              @Override
              public void run(Universe universe) {
                UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
                NodeDetails node = universe.getNode(nodeName);
                node.cronsActive = false;
                log.info(
                    "Updated "
                        + nodeName
                        + " cronjob status to inactive from universe "
                        + taskParams().universeUUID);
              }
            };
        saveUniverseDetails(updater);

        AlertFilter filter =
            AlertFilter.builder()
                .customerUuid(cust.uuid)
                .errorCode(KnownAlertCodes.CRON_CREATION_FAILURE)
                .label(KnownAlertLabels.TARGET_UUID, universe.universeUUID.toString())
                .build();
        Alert alert =
            alertService
                .listNotResolved(filter)
                .stream()
                .findFirst()
                .orElse(
                    new Alert()
                        .setCustomerUUID(cust.uuid)
                        .setErrCode(KnownAlertCodes.CRON_CREATION_FAILURE)
                        .setType(KnownAlertTypes.Warning)
                        .setLabels(
                            AlertDefinitionLabelsBuilder.create()
                                .appendTarget(universe)
                                .getAlertLabels()));
        if (alert.isNew()) {
          alert.setMessage(String.format(alertMsg, universe.name, nodeName));
        } else {
          List<String> failedNodesList =
              universe
                  .getNodes()
                  .stream()
                  .map(nodeDetail -> nodeDetail.nodeName)
                  .collect(Collectors.toList());
          String failedNodesString = String.join(", ", failedNodesList);
          alert.setMessage(String.format(alertMsg, universe.name, failedNodesString));
        }
        alertService.save(alert);
      }

      // We set the node state to SoftwareInstalled when configuration type is Everything.
      // TODO: Why is upgrade task type used to map to node state update?
      setNodeState(NodeDetails.NodeState.SoftwareInstalled);
    }
  }
}
