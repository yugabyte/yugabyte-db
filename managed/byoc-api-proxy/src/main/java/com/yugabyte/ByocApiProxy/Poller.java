package com.yugabyte.ByocApiProxy;

import com.yugabyte.ByocApiProxy.auth.BaseAuthenticator;
import com.yugabyte.ByocApiProxy.config.ProxiedAppProperties;
import com.yugabyte.ByocApiProxy.config.YbaProperties;
import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.ApiException;
import com.yugabyte.aeon.client.api.InternalQueuedHttpRequestApi;
import com.yugabyte.aeon.client.models.PostQueuedHttpRequestResponseRequestSpec;
import com.yugabyte.aeon.client.models.QueuedHttpRequestData;
import java.io.IOException;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;
import org.springframework.beans.factory.DisposableBean;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Slf4j
@Component
public class Poller implements DisposableBean {
  private static final String X_REQUEST_ID_HEADER = "X-REQUEST-ID";
  private final YbaProperties yba;
  private final ProxiedAppProperties proxiedApp;
  private final BaseAuthenticator authenticator;
  private final ApiClient defaultClient;
  private final HttpClient httpClient;
  private final Supplier<InternalQueuedHttpRequestApi> queuedHttpRequestApiSupplier;
  private final ExecutorService httpDispatchExecutor;

  /**
   * Like {@link java.util.function.Supplier} but allows checked exceptions (e.g. {@link
   * ApiException}).
   */
  @FunctionalInterface
  private interface ThrowingSupplier<T> {
    T get() throws Exception;
  }

  public Poller(
      YbaProperties yba,
      ProxiedAppProperties proxiedApp,
      BaseAuthenticator authenticator,
      ApiClient defaultClient,
      HttpClient httpClient,
      Supplier<InternalQueuedHttpRequestApi> queuedHttpRequestApiSupplier,
      ExecutorService httpDispatchExecutor) {
    this.yba = yba;
    this.proxiedApp = proxiedApp;
    this.authenticator = authenticator;
    this.defaultClient = defaultClient;
    this.httpClient = httpClient;
    this.queuedHttpRequestApiSupplier = queuedHttpRequestApiSupplier;
    this.httpDispatchExecutor = httpDispatchExecutor;
  }

  @Override
  public void destroy() {
    httpDispatchExecutor.shutdown();

    try {
      if (!httpDispatchExecutor.awaitTermination(10, TimeUnit.SECONDS)) {
        httpDispatchExecutor.shutdownNow();
      }
    } catch (InterruptedException e) {
      httpDispatchExecutor.shutdownNow();
      Thread.currentThread().interrupt();
    }
  }

  private MDC.MDCCloseable setRequestId(String requestId) {
    return MDC.putCloseable("request-id", requestId);
  }

  private <T> T withRequestId(String requestId, ThrowingSupplier<T> action) throws Exception {
    if (requestId == null) {
      return action.get();
    }
    try (MDC.MDCCloseable ignored = setRequestId(requestId)) {
      return action.get();
    }
  }

  @Scheduled(fixedDelay = 1L, timeUnit = TimeUnit.SECONDS)
  public void run() throws Exception {
    log.debug(
        "Poller started against YBA {} ({}) and API Server {}",
        yba.uuid(),
        yba.baseUrl(),
        defaultClient.getBasePath());

    InternalQueuedHttpRequestApi requestApi = queuedHttpRequestApiSupplier.get();
    authenticator.authenticate(defaultClient);

    try {
      List<QueuedHttpRequestData> pending =
          requestApi
              .listPendingQueuedHttpRequests(yba.uuid(), proxiedApp.pollBatchSize())
              .getData();
      if (pending == null || pending.isEmpty()) {
        return;
      }

      List<CompletableFuture<Optional<DispatchResult>>> futures = new ArrayList<>(pending.size());
      for (QueuedHttpRequestData reqData : pending) {
        String mdcRequestId =
            Optional.ofNullable(reqData.getHeaders())
                .map(h -> h.get(X_REQUEST_ID_HEADER))
                .map(reqId -> reqId.get(0))
                .orElse(null);

        withRequestId(
            mdcRequestId,
            () -> {
              log.info("Incoming request: {} {}", reqData.getMethod(), reqData.getUri());
              futures.add(
                  CompletableFuture.supplyAsync(
                      () -> {
                        try {
                          return executeHttpExchange(reqData, mdcRequestId);
                        } catch (Exception e) {
                          if (e instanceof CompletionException ce) {
                            throw ce;
                          }
                          // Single unchecked boundary for CompletableFuture#supplyAsync.
                          throw new CompletionException(e);
                        }
                      },
                      httpDispatchExecutor));
              return null;
            });
      }

      for (CompletableFuture<Optional<DispatchResult>> future : futures) {
        Optional<DispatchResult> result;

        try {
          result = future.join();
        } catch (CompletionException e) {
          Throwable cause = e.getCause() != null ? e.getCause() : e;
          if (cause instanceof ApiException apiException) {
            throw apiException;
          }
          log.error("Parallel dispatch failed", cause);
          continue;
        }

        if (result.isEmpty()) {
          continue;
        }

        DispatchResult dispatch = result.get();
        withRequestId(
            dispatch.mdcRequestId,
            () -> {
              if (dispatch.interrupted()) {
                futures.forEach(f -> f.cancel(true));
                Thread.currentThread().interrupt();
                return null;
              }

              requestApi.postQueuedHttpRequestResponse(
                  dispatch.requestId(), dispatch.responseSpec());
              return null;
            });
      }
    } catch (ApiException e) {
      if (e.getCause() instanceof SocketTimeoutException
          || e.getCause() instanceof ConnectException) {
        log.warn(e.getMessage());
      } else {
        throw e;
      }
    }
  }

  /**
   * Performs the outbound HTTP exchange under request-scoped MDC. IO and interrupt are handled
   * inside the supplier; other failures propagate as {@link Exception}.
   */
  private Optional<DispatchResult> executeHttpExchange(
      QueuedHttpRequestData reqData, String mdcRequestId) throws Exception {
    return withRequestId(
        mdcRequestId,
        () -> {
          HttpRequest.Builder reqBuilder =
              HttpRequest.newBuilder()
                  .uri(URI.create(reqData.getUri()))
                  .method(reqData.getMethod(), bodyPublisher(reqData));

          if (reqData.getContentType() != null) {
            reqBuilder.header("Content-Type", reqData.getContentType());
          }

          if (reqData.getHeaders() != null) {
            for (Map.Entry<String, List<String>> headers : reqData.getHeaders().entrySet()) {
              for (String value : headers.getValue()) {
                reqBuilder.header(headers.getKey(), value);
              }
            }
          }

          PostQueuedHttpRequestResponseRequestSpec responseRequestSpec =
              new PostQueuedHttpRequestResponseRequestSpec();
          try {
            HttpResponse<String> resp =
                httpClient.send(reqBuilder.build(), HttpResponse.BodyHandlers.ofString());

            responseRequestSpec
                .responseHeaders(resp.headers().map())
                .responseStatusCode(resp.statusCode());

            log.info("{} - {}", resp.statusCode(), resp.uri());

            if (resp.statusCode() < 400) {
              responseRequestSpec.responseBody(resp.body());
            } else {
              responseRequestSpec.errorMessage(resp.body());
            }

            return Optional.of(
                new DispatchResult(
                    reqData.getId(), mdcRequestId, responseRequestSpec, /* interrupted */ false));
          } catch (IOException e) {
            log.error("Failed request to {}", reqData.getUri(), e);
            return Optional.empty();
          } catch (InterruptedException e) {
            log.error("Interrupted request to {}", reqData.getUri(), e);
            Thread.currentThread().interrupt();
            return Optional.of(
                new DispatchResult(reqData.getId(), mdcRequestId, responseRequestSpec, true));
          }
        });
  }

  private static HttpRequest.BodyPublisher bodyPublisher(QueuedHttpRequestData reqData) {
    if (reqData.getBody() != null) {
      return HttpRequest.BodyPublishers.ofString(reqData.getBody());
    }

    return HttpRequest.BodyPublishers.noBody();
  }

  private record DispatchResult(
      UUID requestId,
      String mdcRequestId,
      PostQueuedHttpRequestResponseRequestSpec responseSpec,
      boolean interrupted) {}
}
