// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import static com.yugabyte.yw.controllers.TokenAuthenticator.API_JWT_HEADER;
import static com.yugabyte.yw.controllers.TokenAuthenticator.API_TOKEN_HEADER;
import static com.yugabyte.yw.controllers.TokenAuthenticator.AUTH_TOKEN_HEADER;
import static com.yugabyte.yw.controllers.TokenAuthenticator.COOKIE_API_TOKEN;
import static com.yugabyte.yw.controllers.TokenAuthenticator.COOKIE_AUTH_TOKEN;
import static com.yugabyte.yw.controllers.TokenAuthenticator.COOKIE_PLAY_SESSION;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ApiUsageCollector;
import com.yugabyte.yw.common.ApiUsageCollector.ClientKey;
import com.yugabyte.yw.common.ApiUsageCollector.DeprecatedApiKey;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YWErrorHandler;
import com.yugabyte.yw.models.common.YbaApi;
import java.util.Arrays;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.stream.Materializer;
import play.mvc.Filter;
import play.mvc.Http;
import play.mvc.Result;
import play.routing.HandlerDef;
import play.routing.Router;

/** Counts /api calls per API version and client for callhome; see {@link ApiUsageCollector}. */
@Slf4j
public class ApiUsageFilter extends Filter {
  // Emitted by openapi_templates/server/routes.mustache from x-yba-api-visibility.
  static final String V2_DEPRECATED_MODIFIER = "ybaApiVisibility=deprecated";

  private static final Pattern ROUTE_PARAM_REGEX = Pattern.compile("<[^>]*>");
  private static final Pattern UNSAFE_CHARS = Pattern.compile("[^A-Za-z0-9._/-]");

  private final ApiUsageCollector collector;
  private final Map<String, Boolean> v1DeprecatedCache = new ConcurrentHashMap<>();

  @Inject
  public ApiUsageFilter(Materializer mat, ApiUsageCollector collector) {
    super(mat);
    this.collector = collector;
  }

  @Override
  public CompletionStage<Result> apply(
      Function<Http.RequestHeader, CompletionStage<Result>> next, Http.RequestHeader rh) {
    if (!rh.path().startsWith("/api/")) {
      return next.apply(rh);
    }
    return next.apply(rh)
        .whenComplete(
            (result, ex) -> {
              try {
                record(rh, result != null ? result.status() : statusOf(ex));
              } catch (Exception e) {
                log.debug("Failed to record API usage for {}", rh.path(), e);
              }
            });
  }

  @VisibleForTesting
  void record(Http.RequestHeader rh, int status) {
    String apiVersion = apiVersion(rh.path());
    String[] client = parseUserAgent(rh.header(Http.HeaderNames.USER_AGENT).orElse(null));
    ClientKey clientKey = new ClientKey(apiVersion, client[0], client[1], authType(rh));
    DeprecatedApiKey deprecatedKey =
        rh.attrs()
            .getOptional(Router.Attrs.HANDLER_DEF)
            .filter(this::isDeprecated)
            .map(
                hd ->
                    new DeprecatedApiKey(
                        apiVersion,
                        hd.verb(),
                        ROUTE_PARAM_REGEX.matcher(hd.path()).replaceAll(""),
                        client[0]))
            .orElse(null);
    collector.record(clientKey, deprecatedKey, status);
  }

  // Exceptions reach YWErrorHandler after the filters; count the status it will respond with.
  private static int statusOf(Throwable ex) {
    return YWErrorHandler.toPlatformServiceException(ex)
        .map(PlatformServiceException::getHttpStatus)
        .orElse(Http.Status.INTERNAL_SERVER_ERROR);
  }

  static String apiVersion(String path) {
    if (path.startsWith("/api/v2/")) {
      return "v2";
    }
    if (path.startsWith("/api/v1/")) {
      return "v1";
    }
    // v1.routes is also mounted under /api without a version.
    return "v1-unversioned";
  }

  /**
   * Returns {name, version} from the first product token, e.g. "yba-cli/2025.2.0 (darwin)" gives
   * {"yba-cli", "2025.2.0"}. Browser User-Agents are collapsed so they don't fan out into one entry
   * per browser build.
   */
  static String[] parseUserAgent(String userAgent) {
    if (StringUtils.isBlank(userAgent)) {
      return new String[] {"unknown", ""};
    }
    String token = userAgent.trim().split("\\s+", 2)[0];
    if (token.startsWith("Mozilla/")) {
      return new String[] {"browser", ""};
    }
    String name = StringUtils.substringBefore(token, "/");
    String version = StringUtils.substringAfter(token, "/");
    return new String[] {sanitize(name, 64), sanitize(version, 32)};
  }

  private static String sanitize(String s, int maxLength) {
    return StringUtils.left(UNSAFE_CHARS.matcher(s).replaceAll("_"), maxLength);
  }

  static String authType(Http.RequestHeader rh) {
    if (rh.header(API_TOKEN_HEADER).isPresent() || rh.cookie(COOKIE_API_TOKEN).isPresent()) {
      return "api_token";
    }
    if (rh.header(API_JWT_HEADER).isPresent()) {
      return "jwt";
    }
    if (rh.header(AUTH_TOKEN_HEADER).isPresent()
        || rh.cookie(COOKIE_AUTH_TOKEN).isPresent()
        || rh.cookie(COOKIE_PLAY_SESSION).isPresent()) {
      return "session";
    }
    return "none";
  }

  private boolean isDeprecated(HandlerDef hd) {
    if (hd.getModifiers().contains(V2_DEPRECATED_MODIFIER)) {
      return true;
    }
    return v1DeprecatedCache.computeIfAbsent(
        hd.controller() + "." + hd.method(), k -> hasDeprecatedYbaApi(hd));
  }

  private static boolean hasDeprecatedYbaApi(HandlerDef hd) {
    try {
      Class<?> controller = Class.forName(hd.controller(), false, hd.classLoader());
      return Arrays.stream(controller.getMethods())
          .filter(m -> m.getName().equals(hd.method()))
          .map(m -> m.getAnnotation(YbaApi.class))
          .anyMatch(a -> a != null && a.visibility() == YbaApi.YbaApiVisibility.DEPRECATED);
    } catch (ClassNotFoundException | LinkageError e) {
      return false;
    }
  }
}
