package com.yugabyte.ByocApiProxy.config;

import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.api.InternalQueuedHttpRequestApi;
import java.net.http.HttpClient;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.function.Supplier;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class PollerInfrastructureConfig {

  @Bean
  HttpClient pollerHttpClient() {
    return HttpClient.newBuilder().build();
  }

  @Bean
  Supplier<InternalQueuedHttpRequestApi> internalQueuedHttpRequestApiSupplier(ApiClient apiClient) {
    return () -> new InternalQueuedHttpRequestApi(apiClient);
  }

  @Bean(destroyMethod = "shutdown")
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
