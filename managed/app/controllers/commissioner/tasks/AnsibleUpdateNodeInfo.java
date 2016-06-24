// Copyright (c) Yugabyte, Inc.

package controllers.commissioner.tasks;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common;
import models.commissioner.InstanceInfo;
import play.libs.Json;

public class AnsibleUpdateNodeInfo extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpdateNodeInfo.class);

  public static class Params extends AbstractTaskBase.TaskParamsBase {}

  // Result of the task.
  Map<String, String> resultsMap = new HashMap<String, String>();

  public AnsibleUpdateNodeInfo(Params params) {
    super(params);
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

      // Make the node a tserver. The masters will be configured in a separate step.
      nodeDetails.isTserver = true;

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
