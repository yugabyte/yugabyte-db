// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.params.ITaskParams;

public interface ITask extends Runnable {

  /**
   * Initialize the task by reading various parameters.
   */
  public void initialize(ITaskParams taskParams);

  /**
   * A short name representing the task.
   */
  public String getName();

  /**
   * Return a string representation (usually JSON) of the task details. This is used to describe the
   * task to a user in a read-only mode.
   */
  public JsonNode getTaskDetails();

  /**
   * Run the task. Can throw runtime exception on errors.
   */
  @Override
  public void run();

  /**
   * Returns the percent completed of this task.
   * @return a percentage value from 0 to 100.
   */
  public int getPercentCompleted();

  /**
   * User facing task details.
   * @return a list of UserTaskDetails objects
   */
  public UserTaskDetails getUserTaskDetails();

  /**
   * A friendly string representation of the task used for logging.
   * @return string representation of the task.
   */
  @Override
  public String toString();
}
