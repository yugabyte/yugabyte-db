package com.yugabyte.ByocApiProxy;

import com.yugabyte.ByocApiProxy.auth.ApiKeyAuthenticator;
import com.yugabyte.ByocApiProxy.auth.BaseAuthenticator;
import com.yugabyte.ByocApiProxy.auth.ServiceAccountAuthenticator;
import com.yugabyte.ByocApiProxy.config.ProxiedAppProperties;
import com.yugabyte.aeon.client.ApiClient;
import java.io.FileInputStream;
import java.io.IOException;
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
  @ConditionalOnProperty(name = "proxied-app.auth.type", havingValue = "api_key")
  public BaseAuthenticator apiKeyAuthenticator(ProxiedAppProperties proxiedApp) {
    String apiKey = proxiedApp.auth().apiKey();
    if (apiKey == null) {
      throw new IllegalStateException(
          "proxied_app.auth.api_key is required when auth.type is api_key");
    }
    return new ApiKeyAuthenticator(apiKey);
  }

  @Bean
  @ConditionalOnProperty(
      name = "proxied-app.certificate",
      matchIfMissing = true,
      havingValue = "non_existent")
  public ApiClient defaultClient(ProxiedAppProperties proxiedApp) {
    ApiClient defaultClient = com.yugabyte.aeon.client.Configuration.getDefaultApiClient();
    int timeout = (int) proxiedApp.readTimeout().toMillis();
    return defaultClient
        .setBasePath(proxiedApp.baseUrl())
        .setConnectTimeout(timeout)
        .setReadTimeout(timeout);
  }

  @Bean
  @ConditionalOnProperty(name = "proxied-app.certificate")
  public ApiClient defaultClientWithCertificate(ProxiedAppProperties proxiedApp)
      throws IOException {
    try (FileInputStream fis = new FileInputStream(proxiedApp.certificate())) {
      return defaultClient(proxiedApp).setSslCaCert(fis);
    }
  }
}
