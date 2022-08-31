// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.inject.ApplicationLifecycle;

/**
 * This allows setting order of shutdown hook execution. ApplicationLifecycle of play framework
 * performs shutdown invocation serially in the reverse order of hook registration. In some cases,
 * it is not desired. For example, TaskExecutor needs to be shutdown first before the thread-pool
 * executors to make sure that running tasks are aborted with shutdown message.
 */
@Slf4j
@Singleton
@Setter
public class ShutdownHookHandler {

  private final ApplicationLifecycle lifecycle;
  private final ExecutorService shutdownExecutor;
  private final List<Hook> hooks;
  // Setting this to true makes it behave like addStopHook of ApplicationLifecycle
  // that invokes the hooks serially.
  private boolean isSerialShutdown = false;

  @Getter
  private static class Hook implements Comparable<Hook> {
    private final Runnable runnable;
    private final int weight;
    private final int time;

    Hook(Runnable runnable, int weight, int time) {
      this.runnable = runnable;
      this.weight = weight;
      this.time = time;
    }

    @Override
    public int compareTo(Hook o) {
      // Descending such that greater weights are submitted first
      // to the execution service.
      if (weight == o.weight) {
        // Later ones are submitted first similar to addStopHook of ApplicationLifecycle.
        return o.time - time;
      }
      return o.weight - weight;
    }
  }

  @Inject
  public ShutdownHookHandler(ApplicationLifecycle lifecycle) {
    this.lifecycle = lifecycle;
    this.shutdownExecutor = Executors.newCachedThreadPool();
    this.hooks = new ArrayList<>();
    this.lifecycle.addStopHook(
        () -> {
          return CompletableFuture.runAsync(this::onApplicationShutdown, shutdownExecutor);
        });
  }

  /**
   * Registers a callback to be invoked on application shutdown.
   *
   * @param runnable the callback.
   */
  public void addShutdownHook(Runnable runnable) {
    addShutdownHook(0, runnable);
  }

  /**
   * Registers a callback to be invoked on application shutdown. Hooks with same weights are
   * executed either concurrently or serially based on isSerialShutdown flag.
   *
   * @param weight the precedence for ordering. Higher the value, higher is the precedence.
   * @param runnable the callback.
   */
  public synchronized void addShutdownHook(int weight, Runnable runnable) {
    int lastTime = hooks.size() > 0 ? hooks.get(hooks.size() - 1).getTime() : 0;
    hooks.add(new Hook(runnable, weight, lastTime + 1));
  }

  @VisibleForTesting
  void onApplicationShutdown() {
    Collections.sort(hooks);
    int pos = 0;
    while (pos < hooks.size()) {
      Map<Hook, Future<?>> futures = new HashMap<>();
      Hook currHook = hooks.get(pos);
      futures.put(currHook, shutdownExecutor.submit(currHook.getRunnable()));
      pos++;
      if (!isSerialShutdown) {
        // Hooks with the same weights are executed concurrently.
        for (; pos < hooks.size(); pos++) {
          currHook = hooks.get(pos);
          if (hooks.get(pos - 1).getWeight() == currHook.getWeight()) {
            futures.put(currHook, shutdownExecutor.submit(currHook.getRunnable()));
          } else {
            break;
          }
        }
      }
      // Wait for completion of the previously submitted shutdown hooks.
      futures
          .entrySet()
          .forEach(
              entry -> {
                try {
                  entry.getValue().get();
                } catch (Exception e) {
                  log.warn("Failed to wait for shutdown of hook {}", entry.getKey(), e);
                }
              });
    }
    shutdownExecutor.shutdownNow();
  }
}
