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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.lang.reflect.Field;
import java.util.Iterator;
import java.util.Map.Entry;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class AnsibleUpdateNodeInfo extends NodeTaskBase {

  @Inject
  protected AnsibleUpdateNodeInfo(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.List, taskParams())
            .processErrors();

    NodeTaskParams taskParams = taskParams();
    log.info(
        "Updating node details for univ uuid={}, node name={} with {}.",
        taskParams.getUniverseUUID(),
        taskParams.nodeName,
        response.message);

    if (Strings.isNullOrEmpty(response.message)) {
      String msg =
          String.format(
              "Node %s in universe %s is not found.",
              taskParams.nodeName, taskParams.getUniverseUUID());
      log.error(msg);
      throw new RuntimeException(msg);
    }

    // Parse into a json object.
    JsonNode jsonNodeTmp = Json.parse(response.message);
    if (jsonNodeTmp.isArray()) {
      jsonNodeTmp = jsonNodeTmp.get(0);
    }
    final JsonNode jsonNode = jsonNodeTmp;

    // Update the node details and persist into the DB.
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            // Get the details of the node to be updated.
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            NodeDetails node = universe.getNode(taskParams.nodeName);
            // Update each field of the node details based on the JSON output.
            Iterator<Entry<String, JsonNode>> iter = jsonNode.fields();
            while (iter.hasNext()) {
              Entry<String, JsonNode> entry = iter.next();
              // Skip null values
              if (entry.getValue().asText() == null) {
                continue;
              }
              Field field;
              try {
                log.debug(
                    "Node {}: setting univ node details field {} to value {}.",
                    taskParams.nodeName,
                    entry.getKey(),
                    entry.getValue());
                // Error out if the host was not found.
                if (entry.getKey().equals("host_found")
                    && entry.getValue().asText().equals("false")) {
                  throw new RuntimeException("Host " + taskParams.nodeName + " not found.");
                }
                try {
                  field = NodeDetails.class.getField(entry.getKey());
                  field.set(node, entry.getValue().asText());
                } catch (NoSuchFieldException eCS) {
                  // Try one more time with the cloud info class.
                  // TODO: May be try this first as it has more fields?
                  field = CloudSpecificInfo.class.getField(entry.getKey());
                  field.set(node.cloudInfo, entry.getValue().asText());
                }
              } catch (NoSuchFieldException | SecurityException e) {
                log.warn(
                    "Skipping field {} with value {} due to error {}.",
                    entry.getKey(),
                    entry.getValue(),
                    e.getMessage());
              } catch (IllegalArgumentException | IllegalAccessException e) {
                log.error(
                    "Field {} could not be updated to value {} due to error {}.",
                    entry.getKey(),
                    entry.getValue(),
                    e.getMessage(),
                    e);
              }
            }
            // Node provisioning completed.
            node.state = NodeDetails.NodeState.Provisioned;
            // Update the node details.
            universeDetails.nodeDetailsSet.add(node);
            universe.setUniverseDetails(universeDetails);
          }
        };
    // Save the updated universe object.
    saveUniverseDetails(updater);
  }
}
