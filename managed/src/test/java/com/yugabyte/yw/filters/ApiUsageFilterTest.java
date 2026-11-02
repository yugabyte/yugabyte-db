// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.filters;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.mvc.Results.status;

import com.yugabyte.yw.common.ApiUsageCollector;
import com.yugabyte.yw.common.ApiUsageCollector.ClientKey;
import com.yugabyte.yw.common.ApiUsageCollector.Counts;
import com.yugabyte.yw.common.ApiUsageCollector.DeprecatedApiKey;
import com.yugabyte.yw.common.ApiUsageCollector.Snapshot;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.common.YbaApi;
import jakarta.persistence.EntityNotFoundException;
import jakarta.persistence.OptimisticLockException;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import org.apache.pekko.actor.ActorSystem;
import org.apache.pekko.stream.Materializer;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import play.api.routing.HandlerDef;
import play.mvc.Http;
import play.routing.Router;
import scala.jdk.javaapi.CollectionConverters;

public class ApiUsageFilterTest {
  private ActorSystem actorSystem;
  private ApiUsageCollector collector;
  private ApiUsageFilter filter;

  public static class FakeController {
    @YbaApi(visibility = YbaApi.YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
    public void oldApi() {}

    @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0.0")
    public void newApi() {}

    @YbaApi(visibility = YbaApi.YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.20.0.0")
    public void overloaded(String name) {}

    @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0.0")
    public void overloaded(boolean flag) {}
  }

  @Before
  public void setup() {
    actorSystem = ActorSystem.create();
    collector = new ApiUsageCollector();
    filter = new ApiUsageFilter(Materializer.matFromSystem(actorSystem), collector);
  }

  @After
  public void teardown() {
    actorSystem.terminate();
  }

  private static HandlerDef handlerDef(String method, String path, List<String> modifiers) {
    return handlerDef(method, List.of(), path, modifiers);
  }

  private static HandlerDef handlerDef(
      String method, List<Class<?>> parameterTypes, String path, List<String> modifiers) {
    return new HandlerDef(
        ApiUsageFilterTest.class.getClassLoader(),
        "router",
        FakeController.class.getName(),
        method,
        CollectionConverters.asScala(parameterTypes).toSeq(),
        "POST",
        path,
        "",
        CollectionConverters.asScala(modifiers).toSeq());
  }

  private Snapshot apply(Http.RequestBuilder builder, int status) {
    filter
        .apply(rh -> CompletableFuture.completedFuture(status(status)), builder.build())
        .toCompletableFuture()
        .join();
    return collector.snapshot(Instant.now());
  }

  @Test
  public void testParseUserAgent() {
    assertArrayEquals(
        new String[] {"yba-cli", "2025.2.0"},
        ApiUsageFilter.parseUserAgent("yba-cli/2025.2.0 (darwin; arm64)"));
    assertArrayEquals(
        new String[] {"OpenAPI-Generator", "2.0.0/go"},
        ApiUsageFilter.parseUserAgent("OpenAPI-Generator/2.0.0/go"));
    assertArrayEquals(
        new String[] {"browser", ""},
        ApiUsageFilter.parseUserAgent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)"));
    assertArrayEquals(new String[] {"curl", "8.7.1"}, ApiUsageFilter.parseUserAgent("curl/8.7.1"));
    assertArrayEquals(new String[] {"unknown", ""}, ApiUsageFilter.parseUserAgent(null));
    assertArrayEquals(new String[] {"a_b", ""}, ApiUsageFilter.parseUserAgent("a\"b"));
  }

  @Test
  public void testApiVersion() {
    assertEquals("v2", ApiUsageFilter.apiVersion("/api/v2/customers/x/universes"));
    assertEquals("v1", ApiUsageFilter.apiVersion("/api/v1/customers/x/universes"));
    assertEquals("v1-unversioned", ApiUsageFilter.apiVersion("/api/customers/x/universes"));
  }

  @Test
  public void testRecordsClient() {
    Snapshot snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v1/customers")
                .header("User-Agent", "yba-cli/2025.2.0")
                .header("X-AUTH-YW-API-TOKEN", "token"),
            200);
    assertEquals(
        new Counts(1, 0, 0),
        snapshot.clients().get(new ClientKey("v1", "yba-cli", "2025.2.0", "api_token")));
    assertTrue(snapshot.deprecatedApis().isEmpty());
  }

  @Test
  public void testIgnoresNonApiPaths() {
    Snapshot snapshot = apply(new Http.RequestBuilder().uri("/static/main.js"), 200);
    assertTrue(snapshot.clients().isEmpty());
  }

  @Test
  public void testDeprecatedV1Api() {
    Snapshot snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v1/customers/abc/old")
                .header("User-Agent", "curl/8.7.1")
                .attr(
                    Router.Attrs.HANDLER_DEF,
                    handlerDef("oldApi", "/api/v1/customers/$cUUID<[^/]+>/old", List.of())),
            400);
    assertEquals(
        new Counts(1, 1, 0),
        snapshot
            .deprecatedApis()
            .get(new DeprecatedApiKey("v1", "POST", "/api/v1/customers/$cUUID/old", "curl")));

    snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v1/customers/abc/new")
                .attr(
                    Router.Attrs.HANDLER_DEF,
                    handlerDef("newApi", "/api/v1/customers/$cUUID<[^/]+>/new", List.of())),
            200);
    assertEquals(1, snapshot.deprecatedApis().size());
  }

  @Test
  public void testDeprecatedOverload() {
    // Only the overload the route resolves to counts; the route's boxed Boolean matches boolean.
    Snapshot snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v1/flag")
                .attr(
                    Router.Attrs.HANDLER_DEF,
                    handlerDef("overloaded", List.of(Boolean.class), "/api/v1/flag", List.of())),
            200);
    assertTrue(snapshot.deprecatedApis().isEmpty());

    snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v1/name")
                .attr(
                    Router.Attrs.HANDLER_DEF,
                    handlerDef("overloaded", List.of(String.class), "/api/v1/name", List.of())),
            200);
    assertEquals(
        new Counts(1, 0, 0),
        snapshot
            .deprecatedApis()
            .get(new DeprecatedApiKey("v1", "POST", "/api/v1/name", "unknown")));
  }

  @Test
  public void testDeprecatedV2Api() {
    Snapshot snapshot =
        apply(
            new Http.RequestBuilder()
                .uri("/api/v2/customers/abc/thing")
                .attr(
                    Router.Attrs.HANDLER_DEF,
                    handlerDef(
                        "notAnnotated",
                        "/api/v2/customers/$cUUID<[^/]+>/thing",
                        List.of(ApiUsageFilter.V2_DEPRECATED_MODIFIER))),
            200);
    assertEquals(
        new Counts(1, 0, 0),
        snapshot
            .deprecatedApis()
            .get(new DeprecatedApiKey("v2", "POST", "/api/v2/customers/$cUUID/thing", "unknown")));
  }

  private Counts failWith(Throwable ex) {
    ApiUsageCollector collector = new ApiUsageCollector();
    new ApiUsageFilter(Materializer.matFromSystem(actorSystem), collector)
        .apply(
            rh -> CompletableFuture.failedFuture(ex),
            new Http.RequestBuilder()
                .uri("/api/v1/customers")
                .cookie(Http.Cookie.builder("PLAY_SESSION", "x").build())
                .build())
        .exceptionally(e -> null)
        .toCompletableFuture()
        .join();
    return collector
        .snapshot(Instant.now())
        .clients()
        .get(new ClientKey("v1", "unknown", "", "session"));
  }

  @Test
  public void testFailedRequest() {
    assertEquals(new Counts(1, 1, 0), failWith(new PlatformServiceException(403, "Forbidden")));
    // Statuses must match what YWErrorHandler responds with.
    assertEquals(
        new Counts(1, 1, 0),
        failWith(new RuntimeException(new PlatformServiceException(400, "Bad request"))));
    assertEquals(new Counts(1, 1, 0), failWith(new OptimisticLockException("changed")));
    assertEquals(new Counts(1, 0, 1), failWith(new EntityNotFoundException("gone")));
    assertEquals(new Counts(1, 0, 1), failWith(new IllegalStateException("bug")));
  }
}
