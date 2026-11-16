// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;

/**
 * Play action that short-circuits GET with 304 when {@code If-None-Match} matches the resource
 * revision, and attaches {@code ETag} / {@code Cache-Control} on successful 200 responses.
 *
 * <p>No-ops unless {@link GlobalConfKeys#conditionalGetEnabled} is true.
 */
public class ConditionalGetAction extends Action<ConditionalGet> {

  private final com.google.inject.Injector injector;
  private final RuntimeConfigCache runtimeConfigCache;

  @Inject
  public ConditionalGetAction(
      com.google.inject.Injector injector, RuntimeConfigCache runtimeConfigCache) {
    this.injector = injector;
    this.runtimeConfigCache = runtimeConfigCache;
  }

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    if (!runtimeConfigCache.getBoolean(GlobalConfKeys.conditionalGetEnabled.getKey())) {
      return delegate.call(request);
    }

    ResourceRevisionProvider provider = injector.getInstance(configuration.value());
    Optional<ConditionalGets.CustomerAndResource> ids =
        ConditionalGets.extractCustomerAndResource(
            request.path(), configuration.resourcePathSegment());

    if (ids.isEmpty()) {
      return delegate.call(request);
    }

    UUID customerUUID = ids.get().customerUUID();
    UUID resourceUUID = ids.get().resourceUUID();

    // Read the revision before the handler builds the body: if the resource changes in between,
    // the ETag is older than the body and the next request gets a fresh 200. Reading it after
    // would pair an old body with a new ETag and make clients cache stale data behind 304s.
    Optional<ResourceRevision> revision =
        provider
            .find(customerUUID, resourceUUID)
            .map(rev -> ConditionalGets.scopedToYbaVersion(rev, Util.getYbaVersion()));
    Optional<Result> notModified = ConditionalGets.maybeNotModified(request, revision);

    if (notModified.isPresent()) {
      return CompletableFuture.completedFuture(notModified.get());
    }

    return delegate
        .call(request)
        .thenApply(
            result -> {
              if (result.status() != Http.Status.OK) {
                return result;
              }

              return revision
                  .map(rev -> ConditionalGets.attachCacheHeaders(result, rev))
                  .orElse(result);
            });
  }
}
