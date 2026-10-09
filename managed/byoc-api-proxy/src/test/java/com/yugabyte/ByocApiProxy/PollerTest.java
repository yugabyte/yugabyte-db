package com.yugabyte.ByocApiProxy;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.timeout;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.ByocApiProxy.auth.BaseAuthenticator;
import com.yugabyte.ByocApiProxy.config.ProxiedAppProperties;
import com.yugabyte.ByocApiProxy.config.YbaProperties;
import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.ApiException;
import com.yugabyte.aeon.client.api.InternalQueuedHttpRequestApi;
import com.yugabyte.aeon.client.models.PostQueuedHttpRequestResponseRequestSpec;
import com.yugabyte.aeon.client.models.QueuedHTTPRequestListResponse;
import com.yugabyte.aeon.client.models.QueuedHttpRequestData;
import java.io.IOException;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpHeaders;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.HttpTimeoutException;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

@ExtendWith(MockitoExtension.class)
class PollerTest {

  private static final UUID YBA_UUID = UUID.fromString("11111111-1111-1111-1111-111111111111");

  @Mock private YbaProperties yba;
  @Mock private ProxiedAppProperties proxiedApp;
  @Mock private BaseAuthenticator authenticator;
  @Mock private ApiClient defaultClient;
  @Mock private HttpClient httpClient;
  @Mock private InternalQueuedHttpRequestApi requestApi;

  private ExecutorService executor;
  private Poller poller;

  @BeforeEach
  void setUp() {
    executor =
        Executors.newFixedThreadPool(
            8,
            r -> {
              Thread t = new Thread(r, "poller-test");
              t.setDaemon(true);
              return t;
            });
    when(yba.uuid()).thenReturn(YBA_UUID);
    when(yba.baseUrl()).thenReturn("http://yba.test");
    lenient().when(proxiedApp.pollBatchSize()).thenReturn(10);
    lenient().when(proxiedApp.readTimeout()).thenReturn(Duration.ofSeconds(30));
    when(defaultClient.getBasePath()).thenReturn("http://proxied.test/api");

    poller =
        new Poller(
            yba, proxiedApp, authenticator, defaultClient, httpClient, () -> requestApi, executor);
  }

  @AfterEach
  void tearDown() throws Exception {
    poller.destroy();
    executor.shutdownNow();
    executor.awaitTermination(5, TimeUnit.SECONDS);
    while (Thread.interrupted()) {
      Thread.interrupted();
    }
  }

  @Test
  void run_invokesAuthenticate() throws Exception {
    stubPending(null);

    poller.run();

    verify(authenticator).authenticate(defaultClient);
  }

  @Test
  void run_pendingNull_returnsWithoutPosting() throws Exception {
    stubPending(null);

    poller.run();

    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_pendingEmpty_returnsWithoutPosting() throws Exception {
    stubPending(List.of());

    poller.run();

    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_http200_postsResponseWithBody() throws Exception {
    UUID requestId = UUID.fromString("22222222-2222-2222-2222-222222222222");
    String uri = "http://localhost/success";
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(requestId)
            .method("GET")
            .uri(uri)
            .headers(Map.of("X-REQUEST-ID", List.of("req-trace-1")));
    stubPending(List.of(pendingItem));

    HttpResponse<String> httpResponse = mock(HttpResponse.class);
    when(httpResponse.statusCode()).thenReturn(200);
    when(httpResponse.body()).thenReturn("{\"ok\":true}");
    when(httpResponse.uri()).thenReturn(URI.create(uri));
    when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
    when(httpClient.send(any(HttpRequest.class), any())).thenAnswer(invocation -> httpResponse);

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    PostQueuedHttpRequestResponseRequestSpec spec = specCaptor.getValue();
    assertEquals(200, spec.getResponseStatusCode());
    assertEquals("{\"ok\":true}", spec.getResponseBody());
    assertNotNull(spec.getResponseHeaders());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_http4xx_postsErrorMessageNotBody() throws Exception {
    UUID requestId = UUID.fromString("33333333-3333-3333-3333-333333333333");
    String uri = "http://localhost/err";
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri(uri);
    stubPending(List.of(pendingItem));

    HttpResponse<String> httpResponse = mock(HttpResponse.class);
    when(httpResponse.statusCode()).thenReturn(503);
    when(httpResponse.body()).thenReturn("unavailable");
    when(httpResponse.uri()).thenReturn(URI.create(uri));
    when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
    when(httpClient.send(any(HttpRequest.class), any())).thenAnswer(invocation -> httpResponse);

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    PostQueuedHttpRequestResponseRequestSpec spec = specCaptor.getValue();
    assertEquals(503, spec.getResponseStatusCode());
    assertEquals("unavailable", spec.getErrorMessage());
    assertTrue(spec.getResponseBody() == null || spec.getResponseBody().isEmpty());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_setsContentTypeAndBodyOnHttpRequest() throws Exception {
    UUID requestId = UUID.fromString("44444444-4444-4444-4444-444444444444");
    String uri = "http://localhost/post";
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(requestId)
            .method("POST")
            .uri(uri)
            .contentType("application/json")
            .body("{\"a\":1}");
    stubPending(List.of(pendingItem));

    HttpResponse<String> httpResponse = mock(HttpResponse.class);
    when(httpResponse.statusCode()).thenReturn(201);
    when(httpResponse.body()).thenReturn("created");
    when(httpResponse.uri()).thenReturn(URI.create(uri));
    when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
    when(httpClient.send(any(HttpRequest.class), any())).thenAnswer(invocation -> httpResponse);

    poller.run();

    ArgumentCaptor<HttpRequest> reqCaptor = ArgumentCaptor.forClass(HttpRequest.class);
    verify(httpClient, timeout(5000)).send(reqCaptor.capture(), any());
    HttpRequest built = reqCaptor.getValue();
    assertEquals("POST", built.method());
    assertEquals(URI.create("http://yba.test/post"), built.uri());
    assertTrue(built.headers().firstValue("Content-Type").orElse("").contains("application/json"));
    assertEquals(Duration.ofSeconds(30), built.timeout().orElseThrow());
  }

  @Test
  void run_ioExceptionFromHttp_postsBadGateway() throws Exception {
    UUID requestId = UUID.randomUUID();
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri("http://localhost/fail");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new IOException("boom"));

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    assertEquals(502, specCaptor.getValue().getResponseStatusCode());
    assertTrue(specCaptor.getValue().getErrorMessage().contains("boom"));
  }

  @Test
  void run_httpTimeout_postsGatewayTimeout() throws Exception {
    UUID requestId = UUID.randomUUID();
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri("http://localhost/slow");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any()))
        .thenThrow(new HttpTimeoutException("timed out"));

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    assertEquals(504, specCaptor.getValue().getResponseStatusCode());
  }

  @Test
  void run_interruptedHttp_postsServiceUnavailable() throws Exception {
    UUID requestId = UUID.randomUUID();
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri("http://localhost/sleep");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new InterruptedException());

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    assertEquals(503, specCaptor.getValue().getResponseStatusCode());
    assertFalse(Thread.interrupted());
  }

  @Test
  void run_unexpectedHttpFailure_postsInternalErrorWithoutFailingRun() throws Exception {
    UUID requestId = UUID.randomUUID();
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri("http://localhost/x");
    stubPending(List.of(pendingItem));
    ApiException apiEx = new ApiException(500, Map.of(), "body");
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new CompletionException(apiEx));

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    assertEquals(500, specCaptor.getValue().getResponseStatusCode());
  }

  @Test
  void run_joinCompletionExceptionNonApi_logsAndContinues() throws Exception {
    QueuedHttpRequestData a =
        new QueuedHttpRequestData().id(UUID.randomUUID()).method("GET").uri("http://localhost/a");
    QueuedHttpRequestData b =
        new QueuedHttpRequestData().id(UUID.randomUUID()).method("GET").uri("http://localhost/b");
    stubPending(List.of(a, b));
    AtomicInteger calls = new AtomicInteger();
    when(httpClient.send(any(), any()))
        .thenAnswer(
            inv -> {
              if (calls.getAndIncrement() == 0) {
                throw new CompletionException(new IllegalStateException("first"));
              }
              HttpResponse<String> httpResponse = mock(HttpResponse.class);
              when(httpResponse.statusCode()).thenReturn(200);
              when(httpResponse.body()).thenReturn("ok");
              when(httpResponse.uri()).thenReturn(URI.create("http://localhost/b"));
              when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
              return httpResponse;
            });

    poller.run();

    verify(requestApi, timeout(5000).times(2)).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_authenticateThrowsApiExceptionWithConnectExceptionCause_logsWarn() throws Exception {
    ApiException conn =
        new ApiException("conn", new ConnectException("refused"), 500, Map.of(), "");
    doThrow(conn).when(authenticator).authenticate(defaultClient);

    poller.run();

    verify(requestApi, never()).listPendingQueuedHttpRequests(any(), any());
    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_listPendingThrowsApiExceptionWithSocketTimeoutCause_logsWarn() throws Exception {
    ApiException timeout =
        new ApiException("timeout", new SocketTimeoutException("timed out"), 500, Map.of(), "");
    when(requestApi.listPendingQueuedHttpRequests(eq(YBA_UUID), eq(10))).thenThrow(timeout);

    poller.run();

    verify(authenticator).authenticate(defaultClient);
    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_listPendingThrowsApiExceptionWithConnectExceptionCause_logsWarn() throws Exception {
    ApiException conn =
        new ApiException("conn", new ConnectException("refused"), 500, Map.of(), "");
    when(requestApi.listPendingQueuedHttpRequests(eq(YBA_UUID), eq(10))).thenThrow(conn);

    poller.run();

    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_listPendingThrowsOtherApiException_propagates() throws Exception {
    ApiException fatal = new ApiException("fatal", 500, Map.of(), "");
    when(requestApi.listPendingQueuedHttpRequests(eq(YBA_UUID), eq(10))).thenThrow(fatal);

    assertThrows(ApiException.class, poller::run);
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_postFailsForOne_logsAndContinuesBatch() throws Exception {
    UUID idA = UUID.fromString("77777777-7777-7777-7777-777777777777");
    UUID idB = UUID.fromString("88888888-8888-8888-8888-888888888888");
    QueuedHttpRequestData a =
        new QueuedHttpRequestData().id(idA).method("GET").uri("http://localhost/a");
    QueuedHttpRequestData b =
        new QueuedHttpRequestData().id(idB).method("GET").uri("http://localhost/b");
    stubPending(List.of(a, b));
    when(httpClient.send(any(HttpRequest.class), any()))
        .thenAnswer(
            invocation -> {
              HttpResponse<String> httpResponse = mock(HttpResponse.class);
              when(httpResponse.statusCode()).thenReturn(200);
              when(httpResponse.body()).thenReturn("ok");
              when(httpResponse.uri()).thenReturn(URI.create("http://localhost/x"));
              when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
              return httpResponse;
            });

    // Posting the first response fails (e.g. 413); it must not abort the batch or crash run().
    ApiException postFail = new ApiException("Payload Too Large", 413, Map.of(), "");
    doThrow(postFail).when(requestApi).postQueuedHttpRequestResponse(eq(idA), any());

    poller.run();

    // Both posts are attempted; the second still succeeds despite the first failing.
    verify(requestApi, timeout(5000)).postQueuedHttpRequestResponse(eq(idA), any());
    verify(requestApi, timeout(5000)).postQueuedHttpRequestResponse(eq(idB), any());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_postsFastRequestWithoutWaitingForSlowBatchMate() throws Exception {
    UUID slowId = UUID.fromString("55555555-5555-5555-5555-555555555555");
    UUID fastId = UUID.fromString("66666666-6666-6666-6666-666666666666");
    CountDownLatch slowStarted = new CountDownLatch(1);
    CountDownLatch releaseSlow = new CountDownLatch(1);
    stubPending(
        List.of(
            new QueuedHttpRequestData().id(slowId).method("GET").uri("http://localhost/slow"),
            new QueuedHttpRequestData().id(fastId).method("GET").uri("http://localhost/fast")));
    when(httpClient.send(any(HttpRequest.class), any()))
        .thenAnswer(
            invocation -> {
              HttpRequest request = invocation.getArgument(0);
              if (request.uri().getPath().endsWith("/slow")) {
                slowStarted.countDown();
                assertTrue(releaseSlow.await(5, TimeUnit.SECONDS));
              }
              return okResponse(request.uri());
            });

    poller.run();

    assertTrue(slowStarted.await(5, TimeUnit.SECONDS));
    verify(requestApi, timeout(5000)).postQueuedHttpRequestResponse(eq(fastId), any());
    verify(requestApi, never()).postQueuedHttpRequestResponse(eq(slowId), any());
    releaseSlow.countDown();
    verify(requestApi, timeout(5000)).postQueuedHttpRequestResponse(eq(slowId), any());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_skipsRequestAlreadyInFlight() throws Exception {
    UUID requestId = UUID.fromString("99999999-9999-9999-9999-999999999999");
    CountDownLatch started = new CountDownLatch(1);
    CountDownLatch release = new CountDownLatch(1);
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(requestId).method("GET").uri("http://localhost/dup");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any()))
        .thenAnswer(
            invocation -> {
              started.countDown();
              assertTrue(release.await(5, TimeUnit.SECONDS));
              return okResponse(URI.create("http://localhost/dup"));
            });

    poller.run();
    assertTrue(started.await(5, TimeUnit.SECONDS));
    poller.run();
    release.countDown();

    verify(httpClient, timeout(5000).times(1)).send(any(HttpRequest.class), any());
    verify(requestApi, timeout(5000).times(1)).postQueuedHttpRequestResponse(eq(requestId), any());
  }

  @Test
  void run_expiredClientDeadline_posts408WithoutCallingYba() throws Exception {
    UUID requestId = UUID.randomUUID();
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(requestId)
            .method("GET")
            .uri("http://localhost/late")
            .timeoutOn(OffsetDateTime.now().minusSeconds(1));
    stubPending(List.of(pendingItem));

    poller.run();

    ArgumentCaptor<PostQueuedHttpRequestResponseRequestSpec> specCaptor =
        ArgumentCaptor.forClass(PostQueuedHttpRequestResponseRequestSpec.class);
    verify(requestApi, timeout(5000))
        .postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
    assertEquals(408, specCaptor.getValue().getResponseStatusCode());
    verify(httpClient, never()).send(any(HttpRequest.class), any());
  }

  @Test
  @SuppressWarnings("unchecked")
  void run_honorsTimeoutOnRemainingDeadline() throws Exception {
    UUID requestId = UUID.randomUUID();
    OffsetDateTime timeoutOn = OffsetDateTime.now().plusSeconds(12);
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(requestId)
            .method("GET")
            .uri("http://localhost/ns")
            .timeoutOn(timeoutOn)
            .headers(Map.of("X-REQUEST-ID", List.of("trace-1")));
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any()))
        .thenAnswer(invocation -> okResponse(URI.create("http://localhost/ns")));

    poller.run();

    ArgumentCaptor<HttpRequest> reqCaptor = ArgumentCaptor.forClass(HttpRequest.class);
    verify(httpClient, timeout(5000)).send(reqCaptor.capture(), any());
    Duration timeout = reqCaptor.getValue().timeout().orElseThrow();
    assertTrue(timeout.compareTo(Duration.ofSeconds(13)) <= 0);
    assertTrue(timeout.compareTo(Duration.ofSeconds(1)) >= 0);
  }

  private static HttpResponse<String> okResponse(URI uri) {
    HttpResponse<String> httpResponse = mock(HttpResponse.class);
    when(httpResponse.statusCode()).thenReturn(200);
    when(httpResponse.body()).thenReturn("ok");
    when(httpResponse.uri()).thenReturn(uri);
    when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
    return httpResponse;
  }

  private void stubPending(List<QueuedHttpRequestData> data) throws ApiException {
    QueuedHTTPRequestListResponse response = new QueuedHTTPRequestListResponse();
    response.setData(data);
    when(requestApi.listPendingQueuedHttpRequests(eq(YBA_UUID), eq(10))).thenReturn(response);
  }
}
