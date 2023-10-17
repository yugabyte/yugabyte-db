// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.ITaskParams;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.UUID;

public interface ITask extends Runnable {

  /** Annotation for a ITask class to enable/disable retryable. */
  @Retention(RetentionPolicy.RUNTIME)
  @Target(ElementType.TYPE)
  public @interface Retryable {
    boolean enabled() default true;
  }

  /** Annotation for a ITask class to enable/disable abortable. */
  @Retention(RetentionPolicy.RUNTIME)
  @Target(ElementType.TYPE)
  public @interface Abortable {
    boolean enabled() default true;
  }

  /** Initialize the task by reading various parameters. */
  public void initialize(ITaskParams taskParams);

  /** Clean up the initialization */
  public void terminate();

  /**
   * This is invoked after the task params are initialized for validations before the actual task is
   * created.
   */
  void validateParams();

  /** A short name representing the task. */
  public String getName();

  /**
   * Return a string representation (usually JSON) of the task details. This is used to describe the
   * task to a user in a read-only mode.
   */
  public JsonNode getTaskDetails();

  /** Run the task. Can throw runtime exception on errors. */
  @Override
  public void run();

  /**
   * A friendly string representation of the task used for logging.
   *
   * @return string representation of the task.
   */
  @Override
  public String toString();

  /**
   * Set the user-facing top-level task for the Task tree that this Task belongs to. E.g.
   * CreateUniverse, EditUniverse, etc.
   *
   * @param userTaskUUID UUID of the user-facing top-level task for this Task's Task tree.
   */
  public void setUserTaskUUID(UUID userTaskUUID);
}
