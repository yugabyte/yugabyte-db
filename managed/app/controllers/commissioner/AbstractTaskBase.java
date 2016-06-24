package controllers.commissioner;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;

import controllers.commissioner.Common.CloudType;
import forms.commissioner.ITaskParams;
import play.libs.Json;

public abstract class AbstractTaskBase implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractTaskBase.class);

  public static class TaskParamsBase implements ITaskParams {
    // The cloud provider to get node details.
    public CloudType cloud;
    // The node about which we need to fetch details.
    public String nodeInstanceName;
    // The instance against which this node's details should be saved.
    public UUID instanceUUID;
  }
  protected TaskParamsBase taskParams;

  public AbstractTaskBase(TaskParamsBase params) {
    this.taskParams = params;
    LOG.info("Created task: " + getName() + ", details: " + getTaskDetails());
  }

  @Override
  public void initialize(ITaskParams taskParams) {
    // TODO(bharat): Unused, should be removed once all Tasks implement the above constructor.
  }

  @Override
  public String getName() {
    String classname = this.getClass().getSimpleName();
    return classname + "(" + taskParams.nodeInstanceName + "." + taskParams.cloud + ".yb)";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public abstract void run();

  public void execCommand(String command) {
    LOG.info("Command to run: [" + command + "]");
    try {
      Process p = Runtime.getRuntime().exec(command);

      // Log the stderr output of the process.
      BufferedReader berr = new BufferedReader(new InputStreamReader(p.getErrorStream()));
      String line = null;
      while ( (line = berr.readLine()) != null) {
        LOG.info("[" + getName() + "] STDERR: " + line);
      }
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
