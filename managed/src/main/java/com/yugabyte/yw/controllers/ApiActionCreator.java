// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import java.lang.reflect.Method;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.Done;
import org.apache.pekko.actor.CoordinatedShutdown;
import play.http.ActionCreator;
import play.libs.concurrent.ClassLoaderExecution;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

/**
 * Runs Java controller actions, together with the annotation-based action chain in front of them,
 * on a dedicated thread pool instead of the Pekko default dispatcher.
 *
 * <p>Request handling is mostly blocking (DB, shell commands, remote calls). On the default
 * dispatcher it starves Pekko HTTP and Pekko Streams, so the request body stream may not get its
 * subscriber within pekko.stream.materializer.subscription-timeout and the request fails with
 * "Sink.asPublisher(fanout = false) only supports one subscriber".
 */
@Slf4j
@Singleton
public class ApiActionCreator implements ActionCreator {

  private static final String POOL_NAME = "api_executor";

  private final ExecutorService executor;

  @Inject
  public ApiActionCreator(
      PlatformExecutorFactory executorFactory, CoordinatedShutdown coordinatedShutdown) {
    this.executor =
        // Non-blocking, so a full queue never holds the calling dispatcher thread.
        executorFactory.createNonBlockingExecutor(
            POOL_NAME,
            new ThreadFactoryBuilder().setNameFormat("api-executor-%d").setDaemon(true).build());
    // The server drains in-flight requests up to the service-requests-done phase, and they need
    // the pool until then. Running tasks are not interrupted; a request submitted after this is
    // rejected with 503.
    coordinatedShutdown.addTask(
        CoordinatedShutdown.PhaseServiceStop(),
        "api-executor-shutdown",
        () -> {
          executor.shutdown();
          return CompletableFuture.completedFuture(Done.done());
        });
  }

  @Override
  public Action createAction(Http.Request request, Method actionMethod) {
    return new Action.Simple() {
      @Override
      public CompletionStage<Result> call(Http.Request req) {
        CompletableFuture<Result> result = new CompletableFuture<>();
        try {
          // Keeps the context class loader, which differs from the pool's one in dev mode.
          ClassLoaderExecution.fromThread(executor)
              .execute(
                  () -> {
                    try {
                      delegate
                          .call(req)
                          .whenComplete(
                              (r, e) -> {
                                if (e != null) {
                                  result.completeExceptionally(e);
                                } else {
                                  result.complete(r);
                                }
                              });
                    } catch (Throwable t) {
                      // Fail the same way as if the action had thrown on the calling thread.
                      result.completeExceptionally(t);
                    }
                  });
        } catch (RejectedExecutionException e) {
          log.warn("Rejecting {} {}: request handling queue is full", req.method(), req.uri());
          return CompletableFuture.completedFuture(
              Results.status(Http.Status.SERVICE_UNAVAILABLE, "Too many requests in progress"));
        }
        return result;
      }
    };
  }
}
