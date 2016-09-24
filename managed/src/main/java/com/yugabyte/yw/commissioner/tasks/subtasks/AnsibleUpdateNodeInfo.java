// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.lang.reflect.Field;
import java.util.Iterator;
import java.util.Map.Entry;

import com.yugabyte.yw.commissioner.Common;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UniverseDetails;

import play.libs.Json;

public class AnsibleUpdateNodeInfo extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpdateNodeInfo.class);

  public static class Params extends NodeTaskParams {
  }

  @Override
  public void run() {
    try {
      // Create the process to fetch information about the node from the cloud provider.
      String command = "ybcloud.py " + taskParams().cloud;

      if (taskParams().cloud == Common.CloudType.aws) {
        command += " --region " + taskParams().getRegion().code;
      }

      command += " instance list " + taskParams().nodeName + " --as_json";

      LOG.info("Command to run: [{}]", command);

      // Run the process.
      Process p = Runtime.getRuntime().exec(command);

      // Read the stdout from the process, the result is printed out here.
      BufferedReader bout = new BufferedReader(new InputStreamReader(p.getInputStream()));
      // We expect a single line of json output.
      String line = bout.readLine();
      int exitValue = p.waitFor();
      LOG.info("Command [{}] finished with exit code {}.", command, exitValue);
      // TODO: log output stream somewhere.

      LOG.info("Updating details uuid={}, name={}.",
        taskParams.universeUUID, taskParams.nodeName);

      // Parse into a json object.
      JsonNode jsonNodeTmp = Json.parse(line);
      if (jsonNodeTmp.isArray()) {
        jsonNodeTmp = jsonNodeTmp.get(0);
      }
      final JsonNode jsonNode = jsonNodeTmp;

      // Update the node details and persist into the DB.
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          // Get the details of the node to be updated.
          UniverseDetails universeDetails = universe.getUniverseDetails();
          NodeDetails node = universeDetails.nodeDetailsMap.get(taskParams.nodeName);
          // Update each field of the node details based on the JSON output.
          Iterator<Entry<String, JsonNode>> iter = jsonNode.fields();
          while (iter.hasNext()) {
            Entry<String, JsonNode> entry = iter.next();
            Field field;
            try {
              LOG.info("Node {}: setting field {} to value {}.",
                       taskParams.nodeName, entry.getKey(), entry.getValue());
              // Error out if the host was not found.
              if (entry.getKey().equals("host_found") && entry.getValue().equals("false")) {
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
              LOG.warn("Skipping field {} with value {}.", entry.getKey(), entry.getValue());
            } catch (IllegalArgumentException | IllegalAccessException e) {
              LOG.error("Field {} could not be updated to value {}.",
                        entry.getKey(), entry.getValue(), e);
            }
          }
          // Update the node details.
          universeDetails.nodeDetailsMap.put(taskParams.nodeName, node);
          universe.setUniverseDetails(universeDetails);
        }
      };
      // Save the updated universe object.
      Universe.saveDetails(taskParams.universeUUID, updater);
    } catch (IOException e) {
      throw new RuntimeException(e);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }
}
