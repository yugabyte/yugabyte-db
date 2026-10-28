// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import kamon.Kamon;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.actor.CoordinatedShutdown;

/**
 * This allows setting order of shutdown hook execution. ApplicationLifecycle of play framework
 * performs shutdown invocation serially in the reverse order of hook registration. In some cases,
 * it is not desired. For example, TaskExecutor needs to be shutdown first before the thread-pool
 * executors to make sure that running tasks are aborted with shutdown message.
 */
@Slf4j
@Singleton
public class ShutdownHookHandler {

  private final ExecutorService shutdownExecutor;
  private final Map<Object, Hook<?>> hooks;
  private final AtomicBoolean isShutdownInitiated = new AtomicBoolean();
  private final AtomicBoolean isShutdownComplete = new AtomicBoolean();
  // Setting this to true makes it behave like addStopHook of ApplicationLifecycle
  // that invokes the hooks serially.
  private boolean isSerialShutdown = false;
  private int lastTime = 0;

  /** ShutdownPhase is used to determine the phase of shutdown. */
  public enum ShutdownPhase {
    BEFORE_SERVICE_UNBIND(0),
    AFTER_SERVICE_UNBIND(1);

    // Defining order is more deterministic than relying on enum ordinal.
    // It is also more readable.
    private final int shutdownOrder;
    private static final int maxShutdownOrder;

    static {
      int max = 0;
      for (ShutdownPhase phase : values()) {
        if (phase.shutdownOrder > max) {
          max = phase.shutdownOrder;
        }
      }
      maxShutdownOrder = max;
    }

    ShutdownPhase(int shutdownOrder) {
      this.shutdownOrder = shutdownOrder;
    }

    public int getShutdownOrder() {
      return shutdownOrder;
    }

    public boolean isInitialPhase() {
      return shutdownOrder == 0;
    }

    public boolean isFinalPhase() {
      return shutdownOrder == maxShutdownOrder;
    }
  }

  @Getter
  private static class Hook<T> implements Comparable<Hook<?>>, Runnable {
    private final WeakReference<T> referentRef;
    private final Consumer<T> consumer;
    private final ShutdownPhase phase;
    private final int weight;
    private final int time;

    Hook(ShutdownPhase phase, T referent, Consumer<T> consumer, int weight, int time) {
      // Key in the map cannot be directly referred as it can create strong reference.
      this.referentRef = new WeakReference<>(referent);
      this.consumer = consumer;
      this.phase = phase;
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
  public ShutdownHookHandler(CoordinatedShutdown coordinatedShutdown) {
    this.shutdownExecutor = Executors.newCachedThreadPool();
    this.hooks = new WeakHashMap<>();

    // This works only in Prod mode. In Dev mode, the server uses a separate ActorSystem and unbinds
    // the server port before Application CS runs, so late-request 503s are not guaranteed there.
    // Handling dev mode is too much of hack to locate the dev ActorSystem and unbind the port.
    coordinatedShutdown.addTask(
        CoordinatedShutdown.PhaseBeforeServiceUnbind(),
        getClass().getSimpleName() + "-reject-requests",
        () -> {
          shutdownHooks(ShutdownPhase.BEFORE_SERVICE_UNBIND);
          return CompletableFuture.completedFuture(null);
        });
    coordinatedShutdown.addTask(
        CoordinatedShutdown.PhaseServiceRequestsDone(),
        getClass().getSimpleName() + "-application-shutdown",
        () -> {
          shutdownHooks(ShutdownPhase.AFTER_SERVICE_UNBIND);
          return CompletableFuture.completedFuture(null);
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
    hooks.put(
        referent,
        new Hook<T>(ShutdownPhase.AFTER_SERVICE_UNBIND, referent, consumer, weight, lastTime++));
  }

  /**
   * Registers a callback to be invoked on application shutdown with a key and phase. Hooks with
   * same weights are executed either concurrently or serially based on isSerialShutdown flag. When
   * the referent is garbage collected, the hook is removed.
   *
   * @param phase the phase of shutdown.
   * @param referent the referent object to manage the removal.
   * @param consumer the precedence for ordering. Higher the value, higher is the precedence.
   * @param weight the callback.
   */
  public synchronized <T> void addShutdownHook(
      ShutdownPhase phase, T referent, Consumer<T> consumer, int weight) {
    hooks.put(referent, new Hook<T>(phase, referent, consumer, weight, lastTime++));
  }

  /**
   * Method to check if shutdown is triggered.
   *
   * @return Returns true if it is being shut down, else false.
   */
  public boolean isShutdownInitiated() {
    return isShutdownInitiated.get();
  }

  /**
   * Method to check if shutdown is complete.
   *
   * @return Returns true if it is complete, else false.
   */
  public boolean isShutdownComplete() {
    return isShutdownComplete.get();
  }

  public void shutdownHooks(ShutdownPhase phase) {
    try {
      if (phase.isInitialPhase()) {
        if (isShutdownInitiated.compareAndSet(false, true)) {
          Util.YBA_SHUTDOWN_STARTED = true;
          log.info("Rejecting new requests with 503 until the HTTP port unbinds");
        }
      }
      if (!isShutdownInitiated.get()) {
        throw new IllegalStateException("Shutdown is not initiated yet");
      }
      List<Hook<?>> list =
          hooks.values().stream()
              .filter(hook -> hook.phase == phase)
              .sorted()
              .collect(Collectors.toCollection(ArrayList::new));
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
    } finally {
      if (phase.isFinalPhase()) {
        try {
          Kamon.stop();
        } finally {
          shutdownExecutor.shutdownNow();
          isShutdownComplete.set(true);
        }
      }
    }
  }
}
