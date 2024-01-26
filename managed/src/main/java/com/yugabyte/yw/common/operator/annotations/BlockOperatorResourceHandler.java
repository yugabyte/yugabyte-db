// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator.annotations;

import com.google.inject.Inject;
import com.google.re2j.Matcher;
import com.google.re2j.Pattern;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Slf4j
public class BlockOperatorResourceHandler extends Action<BlockOperatorResource> {

  private static final String UUID_PATTERN =
      "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})";

  private final RuntimeConfigCache confGetter;

  @Inject
  BlockOperatorResourceHandler(RuntimeConfigCache confGetter) {
    this.confGetter = confGetter;
  }

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    if (!enabled()) {
      log.trace("block operator api is disable, skipping action");
      return delegate.call(request);
    }

    switch (configuration.resource()) {
      case UNIVERSE:
        {
          UUID universeUUID = null;
          Pattern uniRegex = Pattern.compile(String.format(".*/universes/%s/?.*", UUID_PATTERN));
          Matcher uniMatcher = uniRegex.matcher(request.path());
          if (uniMatcher.find()) {
            universeUUID = UUID.fromString(uniMatcher.group(1));
          }
          if (universeUUID == null) {
            log.debug("No universe UUID found, assuming resource is not owned by the operator");
            return delegate.call(request);
          }

          Universe universe = Universe.getOrBadRequest(universeUUID);
          if (universe.getUniverseDetails().isKubernetesOperatorControlled) {
            log.warn(
                "blocking api call %s, universe is owned by kubernetes operator", request.path());
            return CompletableFuture.completedFuture(
                Results.forbidden("resource is controlled by kubernetes operator"));
          }
        }
      default:
        {
          log.warn("unknown resource %s, skipping operator owned check", configuration.resource());
          return delegate.call(request);
        }
    }
  }

  private Boolean enabled() {
    return confGetter.getBoolean(GlobalConfKeys.blockOperatorApiResources.getKey());
  }
}
