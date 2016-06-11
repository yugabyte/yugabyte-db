// Copyright (c) Yugabyte, Inc.

package controllers.commissioner;

import com.fasterxml.jackson.databind.JsonNode;

import forms.commissioner.ITaskParams;

public interface ITask {

  /**
   * Initialize the task by reading various parameters.
   */
  public void initialize(ITaskParams taskParams);

  /**
   * Return a string representation (usually JSON) of the task details. This is used to describe the
   * task to a user in a read-only mode.
   */
  public JsonNode getTaskDetails();

  /**
   * Run the task. Can throw exception on errors.
   */
  public void run() throws UnsupportedOperationException;

  /**
   * A friendly string representation of the task used for logging.
   * @return string representation of the task.
   */
  @Override
  public String toString();
}
