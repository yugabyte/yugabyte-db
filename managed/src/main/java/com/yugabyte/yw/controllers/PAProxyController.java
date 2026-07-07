// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.BAD_GATEWAY;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.pa.PerfAdvisorClient;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import play.http.HttpEntity;
import play.libs.ws.SourceBodyWritable;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSResponse;
import play.mvc.Http;
import play.mvc.ResponseHeader;
import play.mvc.Result;

/**
 * Reverse-proxy for Performance Advisor Collector.
 *
 * <p>PA UI (an npm package embedded inside YBA UI) sends its XHRs to this endpoint, same-origin
 * with YBA. This controller authenticates and authorizes the caller via the standard {@link
 * TokenAuthenticator} + {@link com.yugabyte.yw.rbac.annotations.AuthzPath} annotations (so all
 * current and future YBA auth modes work automatically), then forwards the request to the actual PA
 * Collector using the pre-shared {@code X-AUTH-TP-API-TOKEN} service token stored on the {@link
 * PACollector} row. This removes the need for PA to re-validate a user-issued token against YBA,
 * which is what breaks under OIDC/SSO.
 */
@Api(value = "PA Proxy", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PAProxyController extends AuthenticatedController {

  public static final String YB_USER_UUID_HEADER = "X-YB-User-Uuid";
  public static final String YB_USER_EMAIL_HEADER = "X-YB-User-Email";
  public static final String YB_CUSTOMER_UUID_HEADER = "X-YB-Customer-Uuid";

  // Case-insensitive; lowercased.
  private static final Set<String> REQUEST_HEADER_DENY_LIST =
      ImmutableSet.of(
          "cookie",
          "authorization",
          "host",
          "content-length",
          "connection",
          "keep-alive",
          "te",
          "trailer",
          "upgrade",
          "proxy-authorization",
          "proxy-authenticate",
          TokenAuthenticator.AUTH_TOKEN_HEADER.toLowerCase(),
          TokenAuthenticator.API_TOKEN_HEADER.toLowerCase(),
          TokenAuthenticator.API_JWT_HEADER.toLowerCase(),
          "x-auth-tp-token",
          PerfAdvisorClient.TP_API_TOKEN_HEADER.toLowerCase(),
          YB_USER_UUID_HEADER.toLowerCase(),
          YB_USER_EMAIL_HEADER.toLowerCase(),
          YB_CUSTOMER_UUID_HEADER.toLowerCase());

  private static final Set<String> RESPONSE_HEADER_DENY_LIST =
      ImmutableSet.of(
          "set-cookie",
          "authorization",
          "content-length",
          "content-type",
          "transfer-encoding",
          "connection",
          "keep-alive");

  private final WSClientRefresher wsClientRefresher;
  private final PerfAdvisorService perfAdvisorService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public PAProxyController(
      WSClientRefresher wsClientRefresher,
      PerfAdvisorService perfAdvisorService,
      RuntimeConfGetter confGetter) {
    this.wsClientRefresher = wsClientRefresher;
    this.perfAdvisorService = perfAdvisorService;
    this.confGetter = confGetter;
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public CompletionStage<Result> proxyUniverse(
      UUID customerUUID, UUID paUUID, UUID universeUUID, String path, Http.Request request) {
    // Sanity-check that the referenced universe belongs to this customer before forwarding.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);
    String upstreamPath = "universe/" + universeUUID + (path.isEmpty() ? "" : "/" + path);
    return doProxy(customerUUID, paUUID, upstreamPath, request);
  }

  @ApiOperation(value = "UI_ONLY", hidden = true)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public CompletionStage<Result> proxyGeneric(
      UUID customerUUID, UUID paUUID, String path, Http.Request request) {
    return doProxy(customerUUID, paUUID, path, request);
  }

  private CompletionStage<Result> doProxy(
      UUID customerUUID, UUID paUUID, String upstreamPath, Http.Request request) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled)) {
      return CompletableFuture.completedFuture(
          notFound("PA proxy is disabled (yb.pa.embedded_ui.reverse_proxy.enabled=false)"));
    }
    Customer.getOrBadRequest(customerUUID);
    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, paUUID);
    Users user = CommonUtils.getUserFromContext();

    String targetUrl = buildTargetUrl(collector.getPaUrl(), upstreamPath);
    WSClient wsClient = wsClientRefresher.getClient(PerfAdvisorClient.WS_CLIENT_KEY);
    WSRequest wsRequest = wsClient.url(targetUrl);
    wsRequest.setFollowRedirects(false);

    // Forward inbound query params verbatim.
    for (Map.Entry<String, String[]> entry : request.queryString().entrySet()) {
      for (String value : entry.getValue()) {
        wsRequest.addQueryParameter(entry.getKey(), value);
      }
    }

    // Forward safe inbound headers only.
    for (Map.Entry<String, List<String>> entry : request.getHeaders().asMap().entrySet()) {
      if (!isForwardableRequestHeader(entry.getKey())) {
        continue;
      }
      for (String value : entry.getValue()) {
        wsRequest.addHeader(entry.getKey(), value);
      }
    }

    // Inject service auth + user identity claims (for PA-side audit; not used for authz).
    if (StringUtils.isNotEmpty(collector.getPaApiToken())) {
      wsRequest.addHeader(PerfAdvisorClient.TP_API_TOKEN_HEADER, collector.getPaApiToken());
    }
    wsRequest.addHeader(YB_USER_UUID_HEADER, user.getUuid().toString());
    if (StringUtils.isNotEmpty(user.getEmail())) {
      wsRequest.addHeader(YB_USER_EMAIL_HEADER, user.getEmail());
    }
    wsRequest.addHeader(YB_CUSTOMER_UUID_HEADER, customerUUID.toString());

    String method = request.method();
    if (methodAllowsBody(method)) {
      byte[] rawBody = extractRawBody(request);
      if (rawBody != null && rawBody.length > 0) {
        String contentType = request.contentType().orElse(Http.MimeTypes.JSON);
        wsRequest.setBody(
            new SourceBodyWritable(Source.single(ByteString.fromArray(rawBody)), contentType));
      }
    }

    return wsRequest.setMethod(method).stream()
        .thenApply(this::buildProxyResponse)
        .exceptionally(
            ex -> {
              log.warn("PA proxy request to {} {} failed: {}", method, targetUrl, ex.getMessage());
              return status(BAD_GATEWAY, "PA proxy request failed: " + ex.getMessage());
            });
  }

  private Result buildProxyResponse(WSResponse response) {
    Source<ByteString, ?> body = response.getBodyAsSource();

    String contentType =
        response.getSingleHeader(Http.HeaderNames.CONTENT_TYPE).orElse("application/octet-stream");

    // Extract optional Content-Length so downstream can advertise size when known.
    java.util.Optional<Long> contentLength = java.util.Optional.empty();
    java.util.Optional<String> contentLengthHeader =
        response.getSingleHeader(Http.HeaderNames.CONTENT_LENGTH);
    if (contentLengthHeader.isPresent()) {
      try {
        contentLength = java.util.Optional.of(Long.parseLong(contentLengthHeader.get()));
      } catch (NumberFormatException ignored) {
        // fall through with empty
      }
    }

    java.util.Map<String, String> outHeaders = new java.util.HashMap<>();
    for (Map.Entry<String, List<String>> entry : response.getHeaders().entrySet()) {
      if (!isForwardableResponseHeader(entry.getKey()) || entry.getValue().isEmpty()) {
        continue;
      }
      outHeaders.put(entry.getKey(), String.join(",", entry.getValue()));
    }

    ResponseHeader header = new ResponseHeader(response.getStatus(), outHeaders);
    HttpEntity entity =
        new HttpEntity.Streamed(body, contentLength, java.util.Optional.of(contentType));
    return new Result(header, entity);
  }

  private static String buildTargetUrl(String paBaseUrl, String upstreamPath) {
    String base = paBaseUrl;
    while (base.endsWith("/")) {
      base = base.substring(0, base.length() - 1);
    }
    String trimmed = upstreamPath;
    while (trimmed.startsWith("/")) {
      trimmed = trimmed.substring(1);
    }
    String url = base + "/api/" + trimmed;
    // Validate that PA URL didn't smuggle anything weird.
    try {
      new URI(url);
    } catch (URISyntaxException e) {
      throw new PlatformServiceException(BAD_GATEWAY, "Invalid PA target URL: " + url);
    }
    return url;
  }

  private static boolean methodAllowsBody(String method) {
    switch (method.toUpperCase()) {
      case "POST":
      case "PUT":
      case "PATCH":
      case "DELETE":
        return true;
      default:
        return false;
    }
  }

  private static byte[] extractRawBody(Http.Request request) {
    Http.RequestBody body = request.body();
    if (body == null) {
      return null;
    }
    // Try each representation the default parser might have chosen based on Content-Type.
    ByteString bytes = body.asBytes();
    if (bytes != null) {
      return bytes.toArray();
    }
    if (body.asRaw() != null) {
      ByteString raw = body.asRaw().asBytes();
      return raw != null ? raw.toArray() : new byte[0];
    }
    if (body.asJson() != null) {
      return play.libs.Json.stringify(body.asJson())
          .getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }
    if (body.asText() != null) {
      return body.asText().getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }
    return null;
  }

  private static boolean isForwardableRequestHeader(String name) {
    String lower = name.toLowerCase();
    if (REQUEST_HEADER_DENY_LIST.contains(lower)) {
      return false;
    }
    // Never let a browser-supplied YBA/PA identity header pass through.
    return !lower.startsWith("x-yb-");
  }

  private static boolean isForwardableResponseHeader(String name) {
    return !RESPONSE_HEADER_DENY_LIST.contains(name.toLowerCase());
  }
}
