// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common.metrics;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import kamon.Kamon;
import kamon.context.Storage;
import kamon.instrumentation.http.HttpClientInstrumentation;
import kamon.instrumentation.http.HttpMessage;
import kamon.okhttp3.instrumentation.KamonOkHttpTracing$;
import kamon.trace.Span;
import okhttp3.Request;
import org.junit.BeforeClass;
import org.junit.Test;

public class OkHttpOperationNameGeneratorTest {

  @BeforeClass
  public static void initKamon() {
    // Spans stay empty until Kamon is initialized; the kanela agent does it in production.
    Kamon.initWithoutAttaching();
  }

  private static String name(String url) {
    HttpMessage.Request request = mock(HttpMessage.Request.class);
    when(request.url()).thenReturn(url);
    return new OkHttpOperationNameGenerator().name(request).get();
  }

  @Test
  public void testDropsQueryString() {
    assertEquals(
        "http://localhost:9090/api/v1/query",
        name("http://localhost:9090/api/v1/query?start=1730000000&query=up"));
    assertEquals(
        "http://localhost:9090/api/v1/query", name("http://localhost:9090/api/v1/query#frag"));
    assertEquals(
        "https://example.com:6443/api/v1/namespaces",
        name("https://example.com:6443/api/v1/namespaces"));
  }

  @Test
  public void testNullUrl() {
    HttpMessage.Request request = mock(HttpMessage.Request.class);
    assertTrue(new OkHttpOperationNameGenerator().name(request).isEmpty());
  }

  @Test
  public void testOkHttpSpanUsesGenerator() {
    Request request =
        new Request.Builder().url("http://localhost:9090/api/v1/query?start=1&query=up").build();
    // OkHttp client spans are only created under a parent span.
    Span parent = Kamon.spanBuilder("parent").start();
    try (Storage.Scope scope =
        Kamon.storeContext(Kamon.currentContext().withEntry(Span.Key(), parent))) {
      HttpClientInstrumentation.RequestHandler<Request> handler =
          KamonOkHttpTracing$.MODULE$.withNewSpan(request);
      handler.span().finish();
      assertEquals("http://localhost:9090/api/v1/query", handler.span().operationName());
    } finally {
      parent.finish();
    }
  }
}
