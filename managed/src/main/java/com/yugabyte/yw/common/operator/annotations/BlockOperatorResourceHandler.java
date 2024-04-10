// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator.annotations;

import com.google.inject.Inject;
import com.google.re2j.Matcher;
import com.google.re2j.Pattern;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigCache;
import com.yugabyte.yw.models.Provider;
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
        Pattern uniRegex = Pattern.compile(String.format(".*/universes/%s/?.*", UUID_PATTERN));
        UUID universeUUID = getUUIDFromPath(uniRegex, request.path());
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
        return delegate.call(request);
      case PROVIDER:
        Pattern pattern =
            Pattern.compile(String.format(".*/universe/.*/providers/%s/?.*", UUID_PATTERN));
        UUID providerUUID = getUUIDFromPath(pattern, request.path());
        if (providerUUID == null) {
          log.debug("No provider UUID found, assuming resource is not owned by the operator");
          return delegate.call(request);
        }
        Provider provider = Provider.getOrBadRequest(providerUUID);
        if (provider.getDetails().getCloudInfo() != null
            && provider.getDetails().getCloudInfo().kubernetes != null
            && provider.getDetails().getCloudInfo().kubernetes.isKubernetesOperatorControlled) {
          log.warn(
              "blocking api call %s, provider is owned by kubernetes operator", request.path());
          return CompletableFuture.completedFuture(
              Results.forbidden("resource is controlled by kubernetes operator"));
        }
        return delegate.call(request);
      default:
        log.warn("unknown resource %s, skipping operator owned check", configuration.resource());
        return delegate.call(request);
    }
  }

  private UUID getUUIDFromPath(Pattern pattern, String path) {
    Matcher matcher = pattern.matcher(path);
    if (matcher.find()) {
      return UUID.fromString(matcher.group(1));
    }
    return null;
  }

  private Boolean enabled() {
    return confGetter.getBoolean(GlobalConfKeys.blockOperatorApiResources.getKey());
  }
}
