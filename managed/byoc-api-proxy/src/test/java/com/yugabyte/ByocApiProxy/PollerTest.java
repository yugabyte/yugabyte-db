package com.yugabyte.ByocApiProxy;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
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
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletionException;
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
        Executors.newSingleThreadExecutor(
            r -> {
              Thread t = new Thread(r, "poller-test");
              t.setDaemon(true);
              return t;
            });
    when(yba.uuid()).thenReturn(YBA_UUID);
    when(yba.baseUrl()).thenReturn("http://yba.test");
    when(proxiedApp.pollBatchSize()).thenReturn(10);
    when(defaultClient.getBasePath()).thenReturn("http://proxied.test/api");

    poller =
        new Poller(
            yba, proxiedApp, authenticator, defaultClient, httpClient, () -> requestApi, executor);
  }

  @AfterEach
  void tearDown() throws Exception {
    poller.destroy();
    executor.shutdown();
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
    verify(requestApi).postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
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
    verify(requestApi).postQueuedHttpRequestResponse(eq(requestId), specCaptor.capture());
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
    verify(httpClient).send(reqCaptor.capture(), any());
    HttpRequest built = reqCaptor.getValue();
    assertEquals("POST", built.method());
    assertEquals(URI.create(uri), built.uri());
    assertTrue(built.headers().firstValue("Content-Type").orElse("").contains("application/json"));
  }

  @Test
  void run_ioExceptionFromHttp_skipsPost() throws Exception {
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(UUID.randomUUID())
            .method("GET")
            .uri("http://localhost/fail");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new IOException("boom"));

    poller.run();

    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
  }

  @Test
  void run_interruptedHttp_setsInterruptedAndSkipsPost() throws Exception {
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData()
            .id(UUID.randomUUID())
            .method("GET")
            .uri("http://localhost/sleep");
    stubPending(List.of(pendingItem));
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new InterruptedException());

    poller.run();

    verify(requestApi, never()).postQueuedHttpRequestResponse(any(), any());
    assertTrue(Thread.interrupted(), "Poller should restore interrupt after interrupted dispatch");
  }

  @Test
  void run_joinCompletionExceptionWithApiExceptionCause_propagates() throws Exception {
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(UUID.randomUUID()).method("GET").uri("http://localhost/x");
    stubPending(List.of(pendingItem));
    ApiException apiEx = new ApiException(500, Map.of(), "body");
    when(httpClient.send(any(HttpRequest.class), any())).thenThrow(new CompletionException(apiEx));

    ApiException thrown = assertThrows(ApiException.class, poller::run);
    assertEquals(apiEx, thrown);
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

    verify(requestApi).postQueuedHttpRequestResponse(any(), any());
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
  void run_postThrowsApiException_propagates() throws Exception {
    QueuedHttpRequestData pendingItem =
        new QueuedHttpRequestData().id(UUID.randomUUID()).method("GET").uri("http://localhost/ok");
    stubPending(List.of(pendingItem));
    HttpResponse<String> httpResponse = mock(HttpResponse.class);
    when(httpResponse.statusCode()).thenReturn(200);
    when(httpResponse.body()).thenReturn("x");
    when(httpResponse.uri()).thenReturn(URI.create(pendingItem.getUri()));
    when(httpResponse.headers()).thenReturn(mock(HttpHeaders.class));
    when(httpClient.send(any(HttpRequest.class), any())).thenAnswer(invocation -> httpResponse);

    ApiException postFail = new ApiException("post failed", 400, Map.of(), "");
    doThrow(postFail)
        .when(requestApi)
        .postQueuedHttpRequestResponse(
            any(UUID.class), any(PostQueuedHttpRequestResponseRequestSpec.class));

    assertThrows(ApiException.class, poller::run);
  }

  private void stubPending(List<QueuedHttpRequestData> data) throws ApiException {
    QueuedHTTPRequestListResponse response = new QueuedHTTPRequestListResponse();
    response.setData(data);
    when(requestApi.listPendingQueuedHttpRequests(eq(YBA_UUID), eq(10))).thenReturn(response);
  }
}
