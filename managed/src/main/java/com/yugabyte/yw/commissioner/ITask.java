// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.TaskInfo;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.util.UUID;

public interface ITask extends Runnable {

  /** Annotation for a ITask class to enable/disable retryable. */
  @Retention(RetentionPolicy.RUNTIME)
  @Target(ElementType.TYPE)
  @interface Retryable {
    boolean enabled() default true;
  }

  /** Annotation for a ITask class to enable/disable abortable. */
  @Retention(RetentionPolicy.RUNTIME)
  @Target(ElementType.TYPE)
  @interface Abortable {
    boolean enabled() default true;
  }

  /** Initialize the task by reading various parameters. */
  void initialize(ITaskParams taskParams);

  /**
   * This is invoked after the task params are initialized for validations before the actual task is
   * created.
   */
  void validateParams(boolean isFirstTry);

  /** Returns the retry limit on failure. */
  default int getRetryLimit() {
    return 1;
  }

  /**
   * Invoked when the current task fails.
   *
   * @param taskInfo details of the failed task
   * @param cause exception that caused the failure
   * @return true to retry, false to throw {@param cause} up the stack
   */
  default boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    return false;
  }

  /** Clean up the initialization. */
  void terminate();

  /** A short name representing the task. */
  String getName();

  /**
   * Return a string representation (usually JSON) of the task details. This is used to describe the
   * task to a user in a read-only mode.
   */
  JsonNode getTaskParams();

  /**
   * Sets the UUID info of the task. E.g subtask UUID. It is invoked by the task executor.
   *
   * @param taskUUID the task UUID.
   */
  void setTaskUUID(UUID taskUUID);

  /**
   * Set the user-facing top-level task for the Task tree that this Task belongs to. E.g.
   * CreateUniverse, EditUniverse, etc.
   *
   * @param userTaskUUID UUID of the user-facing top-level task for this Task's Task tree.
   */
  void setUserTaskUUID(UUID userTaskUUID);

  /** Returns true if this task has been tried before, else false. */
  boolean isFirstTry();
}
