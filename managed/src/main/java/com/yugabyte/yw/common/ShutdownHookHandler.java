// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Consumer;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
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
public class ShutdownHookHandler {

  private final ApplicationLifecycle lifecycle;
  private final ExecutorService shutdownExecutor;
  private final Map<Object, Hook<?>> hooks;
  // Setting this to true makes it behave like addStopHook of ApplicationLifecycle
  // that invokes the hooks serially.
  private boolean isSerialShutdown = false;
  private int lastTime = 0;

  @Getter
  private static class Hook<T> implements Comparable<Hook<?>>, Runnable {
    private final WeakReference<T> referentRef;
    private final Consumer<T> consumer;
    private final int weight;
    private final int time;

    Hook(T referent, Consumer<T> consumer, int weight, int time) {
      // Key in the map cannot be directly referred as it can create strong reference.
      this.referentRef = new WeakReference<>(referent);
      this.consumer = consumer;
      this.weight = weight;
      this.time = time;
    }

    @Override
    public int compareTo(Hook<?> o) {
      // Descending such that greater weights are submitted first
      // to the execution service.
      if (weight == o.weight) {
        // Later ones are submitted first similar to addStopHook of ApplicationLifecycle.
        return o.time - time;
      }
      return o.weight - weight;
    }

    @Override
    public void run() {
      try {
        T referent = referentRef.get();
        if (referent != null) {
          consumer.accept(referent);
        }
      } catch (Exception e) {
        log.error("Error in running hook {}", this, e);
      }
    }
  }

  @Inject
  public ShutdownHookHandler(ApplicationLifecycle lifecycle) {
    this.lifecycle = lifecycle;
    this.shutdownExecutor = Executors.newCachedThreadPool();
    this.hooks = new WeakHashMap<>();
    this.lifecycle.addStopHook(
        () -> {
          return CompletableFuture.runAsync(this::onApplicationShutdown, shutdownExecutor);
        });
  }

  /**
   * Registers a callback to be invoked on application shutdown with a key. When the referent is
   * garbage collected, the hook is removed.
   *
   * @param referent the referent object to manage the removal.
   * @param runnable the precedence for ordering. Higher the value, higher is the precedence.
   * @param weight the callback.
   */
  public <T> void addShutdownHook(T referent, Consumer<T> consumer) {
    addShutdownHook(referent, consumer, 0);
  }

  /**
   * Registers a callback to be invoked on application shutdown. Hooks with same weights are
   * executed either concurrently or serially based on isSerialShutdown flag. When the referent is
   * garbage collected, the hook is removed.
   *
   * @param referent the referent object to manage the removal.
   * @param runnable the precedence for ordering. Higher the value, higher is the precedence.
   * @param weight the callback.
   */
  public synchronized <T> void addShutdownHook(T referent, Consumer<T> consumer, int weight) {
    hooks.put(referent, new Hook<T>(referent, consumer, weight, lastTime++));
  }

  @VisibleForTesting
  void onApplicationShutdown() {
    List<Hook<?>> list = new ArrayList<>(hooks.values());
    Collections.sort(list);
    int pos = 0;
    while (pos < list.size()) {
      Map<Hook<?>, Future<?>> futures = new HashMap<>();
      Hook<?> currHook = list.get(pos);
      futures.put(currHook, shutdownExecutor.submit(currHook));
      pos++;
      if (!isSerialShutdown) {
        // Hooks with the same weights are executed concurrently.
        for (; pos < list.size(); pos++) {
          currHook = list.get(pos);
          if (list.get(pos - 1).getWeight() == currHook.getWeight()) {
            futures.put(currHook, shutdownExecutor.submit(currHook));
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
