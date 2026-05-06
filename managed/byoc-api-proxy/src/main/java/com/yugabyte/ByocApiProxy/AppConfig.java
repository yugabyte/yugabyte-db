package com.yugabyte.ByocApiProxy;

import com.yugabyte.ByocApiProxy.auth.BaseAuthenticator;
import com.yugabyte.ByocApiProxy.auth.ServiceAccountAuthenticator;
import com.yugabyte.ByocApiProxy.config.ProxiedAppProperties;
import com.yugabyte.aeon.client.ApiClient;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class AppConfig {

  @Bean
  @ConditionalOnProperty(name = "proxied-app.auth.type", havingValue = "service_account")
  public BaseAuthenticator serviceAccountAuthenticator(ProxiedAppProperties proxiedApp) {
    ProxiedAppProperties.ServiceAccount account = proxiedApp.auth().serviceAccount();
    if (account == null) {
      throw new IllegalStateException(
          "proxied_app.auth.service_account is required when auth.type is service_account");
    }
    return new ServiceAccountAuthenticator(account);
  }

  @Bean
  public ApiClient defaultClient(ProxiedAppProperties proxiedApp) {
    ApiClient defaultClient = com.yugabyte.aeon.client.Configuration.getDefaultApiClient();
    int timeout = (int) proxiedApp.readTimeout().toMillis();
    return defaultClient
        .setBasePath(proxiedApp.baseUrl())
        .setConnectTimeout(timeout)
        .setReadTimeout(timeout);
  }
}
