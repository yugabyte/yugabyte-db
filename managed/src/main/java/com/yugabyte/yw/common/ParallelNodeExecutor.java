// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import lombok.Builder;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;

/**
 * Utility for executing tasks on multiple nodes in parallel. Handles executor lifecycle, Future
 * management, timeout handling, and exception handling. Used by NodeScriptRunner,
 * NodeFileCollector, and other node operation services.
 */
@Slf4j
public class ParallelNodeExecutor {

  /** Maximum threads for parallel execution to prevent OOM/thrashing */
  public static final int MAX_PARALLEL_NODES = 50;

  /** Status of a completed task */
  public enum TaskStatus {
    SUCCESS, // Task completed successfully
    TIMEOUT, // Task timed out waiting for result
    REJECTED, // Task was rejected by executor (pool full)
    FAILED // Task threw an exception during execution
  }

  /** Result wrapper for a single node's task execution */
  @Data
  @Builder
  public static class TaskOutcome<R> {
    private NodeDetails node;
    private TaskStatus status;
    private R result; // Non-null if SUCCESS
    private String errorMessage; // Non-null if not SUCCESS
    private long executionTimeMs;
  }

  /**
   * Execute a task on multiple nodes in parallel and collect results. Handles all executor
   * lifecycle, timeout, and exception management.
   *
   * @param nodes List of nodes to execute on
   * @param task Function to execute on each node, returns result of type R
   * @param maxParallelNodes Maximum concurrent executions (capped at MAX_PARALLEL_NODES)
   * @param timeoutSecs Timeout for waiting on each task's result
   * @param executorFactory Factory for creating the thread pool
   * @param executorName Name for the executor (used in logging)
   * @param <R> Type of result returned by each task
   * @return Map of node name to task outcome
   */
  public static <R> Map<String, TaskOutcome<R>> execute(
      List<NodeDetails> nodes,
      Function<NodeDetails, R> task,
      int maxParallelNodes,
      long timeoutSecs,
      PlatformExecutorFactory executorFactory,
      String executorName) {

    Map<String, TaskOutcome<R>> outcomes = new LinkedHashMap<>();

    if (nodes == null || nodes.isEmpty()) {
      log.debug("No nodes provided for parallel execution, returning empty results");
      return outcomes;
    }

    // Cap parallel nodes to prevent resource exhaustion
    int effectiveParallelNodes =
        Math.min(maxParallelNodes > 0 ? maxParallelNodes : MAX_PARALLEL_NODES, MAX_PARALLEL_NODES);
    int poolSize = Math.min(nodes.size(), effectiveParallelNodes);

    ThreadPoolExecutor executor =
        executorFactory.createFixedExecutor(
            executorName, poolSize, Executors.defaultThreadFactory());

    try {
      // Submit all tasks and track node -> future mapping
      Map<NodeDetails, Future<TaskOutcome<R>>> futureMap = new LinkedHashMap<>();

      for (NodeDetails node : nodes) {
        try {
          Future<TaskOutcome<R>> future =
              executor.submit(
                  () -> {
                    long startTime = System.currentTimeMillis();
                    try {
                      R result = task.apply(node);
                      long execTime = System.currentTimeMillis() - startTime;
                      return TaskOutcome.<R>builder()
                          .node(node)
                          .status(TaskStatus.SUCCESS)
                          .result(result)
                          .executionTimeMs(execTime)
                          .build();
                    } catch (Exception e) {
                      long execTime = System.currentTimeMillis() - startTime;
                      log.error(
                          "Error executing task on node {}: {}", node.nodeName, e.getMessage(), e);
                      return TaskOutcome.<R>builder()
                          .node(node)
                          .status(TaskStatus.FAILED)
                          .errorMessage(e.getMessage())
                          .executionTimeMs(execTime)
                          .build();
                    }
                  });
          futureMap.put(node, future);
        } catch (RejectedExecutionException e) {
          log.error("Failed to submit task for node {}: {}", node.nodeName, e.getMessage());
          outcomes.put(
              node.nodeName,
              TaskOutcome.<R>builder()
                  .node(node)
                  .status(TaskStatus.REJECTED)
                  .errorMessage("Failed to submit task: " + e.getMessage())
                  .executionTimeMs(0)
                  .build());
        }
      }

      // Wait for results with timeout
      for (Map.Entry<NodeDetails, Future<TaskOutcome<R>>> entry : futureMap.entrySet()) {
        NodeDetails node = entry.getKey();
        Future<TaskOutcome<R>> future = entry.getValue();

        try {
          TaskOutcome<R> outcome = future.get(timeoutSecs, TimeUnit.SECONDS);
          outcomes.put(node.nodeName, outcome);
        } catch (TimeoutException e) {
          log.error("Timeout waiting for task on node {}", node.nodeName);
          future.cancel(true);
          outcomes.put(
              node.nodeName,
              TaskOutcome.<R>builder()
                  .node(node)
                  .status(TaskStatus.TIMEOUT)
                  .errorMessage("Timed out waiting for task execution")
                  .executionTimeMs(timeoutSecs * 1000)
                  .build());
        } catch (InterruptedException e) {
          log.error("Interrupted waiting for task on node {}", node.nodeName);
          Thread.currentThread().interrupt();
          outcomes.put(
              node.nodeName,
              TaskOutcome.<R>builder()
                  .node(node)
                  .status(TaskStatus.FAILED)
                  .errorMessage("Interrupted: " + e.getMessage())
                  .executionTimeMs(0)
                  .build());
        } catch (Exception e) {
          log.error("Error getting task result for node {}: {}", node.nodeName, e.getMessage(), e);
          outcomes.put(
              node.nodeName,
              TaskOutcome.<R>builder()
                  .node(node)
                  .status(TaskStatus.FAILED)
                  .errorMessage("Error getting result: " + e.getMessage())
                  .executionTimeMs(0)
                  .build());
        }
      }
    } finally {
      executor.shutdownNow();
    }

    return outcomes;
  }
}
