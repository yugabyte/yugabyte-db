// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.Util;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import org.apache.pekko.stream.Materializer;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

/** Returns 503 once YBA shutdown has started (OS signal or CoordinatedShutdown). */
@Singleton
public class ShutdownRejectFilter extends Filter {

  @Inject
  public ShutdownRejectFilter(Materializer mat) {
    super(mat);
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (Util.hasYBAShutdownStarted()) {
      return CompletableFuture.completedFuture(
          Results.status(Http.Status.SERVICE_UNAVAILABLE, "YBA is shutting down"));
    }
    return next.apply(rh);
  }
}
