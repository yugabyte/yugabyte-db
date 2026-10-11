// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.annotations;

import com.google.re2j.Matcher;
import com.google.re2j.Pattern;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YnpProviderUtil;
import com.yugabyte.yw.models.Provider;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Action;
import play.mvc.Http;
import play.mvc.Result;

@Slf4j
public class BlockYnpManagedProviderHandler extends Action<BlockYnpManagedProvider> {

  private static final String UUID_PATTERN =
      "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})";

  private static final Pattern PROVIDER_PATTERN =
      Pattern.compile(String.format(".*/providers/%s/?.*", UUID_PATTERN));

  @Override
  public CompletionStage<Result> call(Http.Request request) {
    UUID providerUUID = getUUIDFromPath(PROVIDER_PATTERN, request.path());
    if (providerUUID == null) {
      log.debug(
          "No provider UUID found in {}, skipping YNP managed provider check", request.path());
      return delegate.call(request);
    }
    Optional<Provider> provider = Provider.maybeGet(providerUUID);
    if (provider.isEmpty()) {
      // Let the controller report the unknown provider.
      return delegate.call(request);
    }
    try {
      YnpProviderUtil.checkYnpManagedProvider(provider.get(), request, configuration.operation());
    } catch (PlatformServiceException e) {
      log.warn("Blocking API call {}, provider is managed by YNP", request.path());
      return CompletableFuture.completedFuture(e.buildResult(request));
    }
    return delegate.call(request);
  }

  private UUID getUUIDFromPath(Pattern pattern, String path) {
    Matcher matcher = pattern.matcher(path);
    if (matcher.find()) {
      return UUID.fromString(matcher.group(1));
    }
    return null;
  }
}
