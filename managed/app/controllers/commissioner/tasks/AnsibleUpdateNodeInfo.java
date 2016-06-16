// Copyright (c) Yugabyte, Inc.

package controllers.commissioner.tasks;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.Common;
import controllers.commissioner.Common.CloudType;
import controllers.commissioner.ITask;
import forms.commissioner.ITaskParams;
import models.commissioner.InstanceInfo;
import play.libs.Json;

public class AnsibleUpdateNodeInfo implements ITask {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpdateNodeInfo.class);

  // Parameters for this task.
  public static class Params implements ITaskParams {
    // The cloud provider to get node details.
    public CloudType cloud;
    // The node about which we need to fetch details.
    public String nodeInstanceName;
    // The instance against which this node's details should be saved.
    public UUID instanceUUID;
  }
  Params taskParams;

  // Result of the task.
  Map<String, String> resultsMap = new HashMap<String, String>();

  @Override
  public void initialize(ITaskParams taskParams) {
    this.taskParams = (Params) taskParams;
  }

  @Override
  public String getName() {
    return "AnsibleUpdateNodeInfo(" + taskParams.nodeInstanceName + "." + taskParams.cloud + ".yb)";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public void run() {
    // Create the process to fetch information about the node from the cloud provider.
    String ybDevopsHome = Common.getDevopsHome();
    String command = ybDevopsHome + "/bin/find_cloud_host.sh" +
                     " " + taskParams.cloud +
                     " " + taskParams.nodeInstanceName +
                     " --json";
    LOG.info("Command to run: [" + command + "]");
    try {
      // Run the process.
      Process p = Runtime.getRuntime().exec(command);

      // Read the stdout from the process, the result is printed out here.
      BufferedReader bout = new BufferedReader(new InputStreamReader(p.getInputStream()));
      String line = null;
      InstanceInfo.NodeDetails nodeDetails = null;
      while ( (line = bout.readLine()) != null) {
        // Skip empty lines.
        if (line.trim().isEmpty()) {
          continue;
        }
        // Parse into a json object.
        JsonNode jsonNode = Json.parse(line);
        nodeDetails = Json.fromJson(jsonNode, InstanceInfo.NodeDetails.class);
      }
      int exitValue = p.waitFor();
      LOG.info("Command [" + command + "] finished with exit code " + exitValue);
      // TODO: log output stream somewhere.

      // Save the updated node details.
      InstanceInfo.updateNodeDetails(taskParams.instanceUUID,
                                     taskParams.nodeInstanceName,
                                     nodeDetails);
    } catch (IOException e) {
      throw new RuntimeException(e);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * Called after the process has completed to retrieve the results.
   * @param key Valid keys are 'public_ip', 'private_ip'.
   * @return Returns the value as a string.
   */
  public String getResult(String key) {
    return resultsMap.get(key);
  }
}
