// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.tasks.AnsibleClusterServerCtl.Params;
import forms.commissioner.ITaskParams;
import forms.commissioner.TaskParamsBase;
import models.commissioner.InstanceInfo;
import util.Util;

public class AnsibleUpdateNodeInfo extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleUpdateNodeInfo.class);

  public static class Params extends TaskParamsBase {
    // True for create instance, false for edit instance.
    public boolean isCreateInstance;
  }

  @Override
  public void run() {
    Params params = (Params)taskParams;

    try {
      // Create the process to fetch information about the node from the cloud provider.
      String ybDevopsHome = Util.getDevopsHome();
      String command = ybDevopsHome + "/bin/find_cloud_host.sh" +
                       " " + taskParams.cloud +
                       " " + taskParams.nodeInstanceName +
                       " --json";
      LOG.info("Command to run: [{}]", command);

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
      LOG.info("Command [{}] finished with exit code {}.", command, exitValue);
      // TODO: log output stream somewhere.

      if (nodeDetails == null) {
        throw new RuntimeException("Updated Node Info failed to return valid nodeDetails.");
      }

      // Make the node a tserver. The masters will be configured in a separate step.
      nodeDetails.isTserver = true;
      nodeDetails.isBeingSetup = !params.isCreateInstance;

      LOG.info("Updating details isCreateInstance={}, uuid={}, name={}.",
          params.isCreateInstance, taskParams.instanceUUID, taskParams.nodeInstanceName);

      // Save the node details, either as a created node or a being added one to new details.
      if (params.isCreateInstance) {
        InstanceInfo.updateNodeDetails(taskParams.instanceUUID,
                                       taskParams.nodeInstanceName,
                                       nodeDetails);
      } else {
        InstanceInfo.updateEditNodeDetails(taskParams.instanceUUID,
                                           taskParams.nodeInstanceName,
                                           nodeDetails);
      }
    } catch (IOException e) {
      throw new RuntimeException(e);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }
}
