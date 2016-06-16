// Copyright (c) Yugabyte, Inc.

package controllers.commissioner.tasks;

import java.io.IOException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.Common;
import controllers.commissioner.Common.CloudType;
import controllers.commissioner.ITask;
import forms.commissioner.ITaskParams;
import play.libs.Json;

public class AnsibleSetupServer implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Parameters for this task.
  public static class Params implements ITaskParams {
    // Needed to run ansible appropriately.
    public CloudType cloud;
    // The node that needs to be provisioned if it already is not.
    public String nodeInstanceName;
    // The VPC into which the node is to be provisioned.
    public String vpcId;
  }

  Params taskParams;

  @Override
  public void initialize(ITaskParams taskParams) {
    this.taskParams = (Params) taskParams;
  }

  @Override
  public String getName() {
    return "AnsibleSetupServer(" + taskParams.nodeInstanceName + "." +
                                   taskParams.vpcId + "." +
                                   taskParams.cloud + ")";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public void run() {
    String ybDevopsHome = Common.getDevopsHome();
    String command = ybDevopsHome + "/bin/setup_server.sh" +
                     " --cloud " + taskParams.cloud +
                     " --instance-name " + taskParams.nodeInstanceName +
                     " --type test-cluster-server";

    // Add the appropriate VPC ID parameter if this is an AWS deployment.
    if (taskParams.cloud == CloudType.aws) {
      command += " --extra-vars aws_vpc_subnet_id=" + taskParams.vpcId;
    }

    LOG.info("Command to run: [" + command + "]");
    try {
      Process p = Runtime.getRuntime().exec(command);
      int exitValue = p.waitFor();
      LOG.info("Command [" + command + "] finished with exit code " + exitValue);
      // TODO: log output stream somewhere.
    } catch (IOException e) {
      throw new RuntimeException(e);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }
}
