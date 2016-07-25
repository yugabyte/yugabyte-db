package com.yugabyte.yw.commissioner;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;

import play.libs.Json;

public abstract class AbstractTaskBase implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(AbstractTaskBase.class);

  protected ITaskParams taskParams;

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName();
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + " : details=" + getTaskDetails();
  }

  @Override
  public abstract void run();

  @Override
  public int getPercentCompleted() {
    return 0;
  }

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
