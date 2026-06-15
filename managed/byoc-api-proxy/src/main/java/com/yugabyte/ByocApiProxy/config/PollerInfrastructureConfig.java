package com.yugabyte.ByocApiProxy.config;

import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.api.InternalQueuedHttpRequestApi;
import java.net.http.HttpClient;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.function.Supplier;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.ssl.SslBundle;
import org.springframework.boot.ssl.SslBundles;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Slf4j
@Configuration
public class PollerInfrastructureConfig {

  static final String YBA_SSL_BUNDLE_NAME = "yba";

  @Bean
  HttpClient pollerHttpClient(SslBundles sslBundles) {
    if (!sslBundles.getBundleNames().contains(YBA_SSL_BUNDLE_NAME)) {
      log.debug("No '{}' SSL bundle configured; using default HttpClient", YBA_SSL_BUNDLE_NAME);
      return HttpClient.newBuilder().build();
    }

    SslBundle sslBundle = sslBundles.getBundle(YBA_SSL_BUNDLE_NAME);
    return HttpClient.newBuilder().sslContext(sslBundle.createSslContext()).build();
  }

  @Bean
  Supplier<InternalQueuedHttpRequestApi> internalQueuedHttpRequestApiSupplier(ApiClient apiClient) {
    return () -> new InternalQueuedHttpRequestApi(apiClient);
  }

  @Bean
  ExecutorService pollerHttpExecutor(ProxiedAppProperties proxiedApp) {
    int poolSize = Math.min(32, Math.max(1, proxiedApp.pollBatchSize()));
    ThreadFactory threadFactory =
        runnable -> {
          Thread t = new Thread(runnable, "byoc-poller-http");
          t.setDaemon(true);
          return t;
        };
    return Executors.newFixedThreadPool(poolSize, threadFactory);
  }
}
