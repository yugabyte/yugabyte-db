package controllers.commissioner.tasks;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.ITask;
import forms.commissioner.ITaskParams;
import play.libs.Json;

public class AnsibleSetupServer implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // The various cloud types supported.
  public enum CloudType {
    aws,
    gcp,
    azu,
  }

  // Parameters for this task.
  public static class Params implements ITaskParams {
    public CloudType cloud;
    public String nodeInstanceName;
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
    String ybDevopsHome = System.getProperty("yb.devops.home");
    if (ybDevopsHome == null) {
      LOG.error("Devops repo path not found. Please specify yb.devops.home property: " +
                "'sbt run -Dyb.devops.home=<path to devops repo>'");
      throw new RuntimeException("Property yb.devops.home was not found.");
    }
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

      InputStream stderr = p.getErrorStream();
      InputStreamReader isr = new InputStreamReader(stderr);
      BufferedReader br = new BufferedReader(isr);
      String line = null;
      System.out.println("<ERROR>");
      while ( (line = br.readLine()) != null) {
        System.out.println(line);
      }
      System.out.println("</ERROR>");

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
