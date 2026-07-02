package com.yugabyte.ByocApiProxy.config;

import com.yugabyte.aeon.client.ApiClient;
import com.yugabyte.aeon.client.api.InternalQueuedHttpRequestApi;
import java.net.Socket;
import java.net.http.HttpClient;
import java.security.GeneralSecurityException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.function.Supplier;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509ExtendedTrustManager;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.ssl.SslBundle;
import org.springframework.boot.ssl.SslBundles;
import org.springframework.boot.ssl.SslManagerBundle;
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
    return HttpClient.newBuilder().sslContext(hostnameAgnosticSslContext(sslBundle)).build();
  }

  /*
   * Builds an {@link SSLContext} from the YBA bundle that retains full certificate chain validation
   * (CA pinning via the bundle truststore) but skips TLS hostname identity verification.
   *
   * <p>The dispatch target is reached by IP literal and its certificate carries an IP SAN only. The
   * JDK {@link HttpClient} reverse-resolves that IP to its PTR name and verifies the certificate
   * against the resolved name, which an IP-only certificate can never satisfy (it fails with "No
   * name matching &lt;ptr&gt; found"). Trust still requires a valid chain to a pinned CA; only the
   * reverse-DNS name check on this internal hop is dropped.
   *
   * <p>The check is dropped at the trust-manager layer rather than via {@link
   * javax.net.ssl.SSLParameters}, because the JDK {@link HttpClient} forces {@code
   * endpointIdentificationAlgorithm="HTTPS"} on the engine and ignores any value supplied through
   * {@code SSLParameters}.
   */
  private static SSLContext hostnameAgnosticSslContext(SslBundle sslBundle) {
    try {
      SslManagerBundle managers = sslBundle.getManagers();
      TrustManager[] trustManagers = managers.getTrustManagers();
      TrustManager[] wrapped = new TrustManager[trustManagers.length];
      for (int i = 0; i < trustManagers.length; i++) {
        TrustManager trustManager = trustManagers[i];
        wrapped[i] =
            trustManager instanceof X509ExtendedTrustManager x509
                ? new HostnameAgnosticTrustManager(x509)
                : trustManager;
      }

      SSLContext sslContext = SSLContext.getInstance(sslBundle.getProtocol());
      sslContext.init(managers.getKeyManagers(), wrapped, null);
      return sslContext;
    } catch (GeneralSecurityException e) {
      throw new IllegalStateException(
          "Failed to build SSL context for '" + YBA_SSL_BUNDLE_NAME + "' bundle", e);
    }
  }

  /*
   * Delegates every trust decision to the JDK trust manager but routes the socket/engine-aware
   * checks through the identity-less {@code checkServerTrusted(chain, authType)} overload. Chain
   * and CA validation still run; only the TLS endpoint (hostname) identity check is bypassed.
   */
  private static final class HostnameAgnosticTrustManager extends X509ExtendedTrustManager {
    private final X509ExtendedTrustManager delegate;

    HostnameAgnosticTrustManager(X509ExtendedTrustManager delegate) {
      this.delegate = delegate;
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType, Socket socket)
        throws CertificateException {
      delegate.checkServerTrusted(chain, authType);
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
        throws CertificateException {
      delegate.checkServerTrusted(chain, authType);
    }

    @Override
    public void checkServerTrusted(X509Certificate[] chain, String authType)
        throws CertificateException {
      delegate.checkServerTrusted(chain, authType);
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType, Socket socket)
        throws CertificateException {
      delegate.checkClientTrusted(chain, authType, socket);
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType, SSLEngine engine)
        throws CertificateException {
      delegate.checkClientTrusted(chain, authType, engine);
    }

    @Override
    public void checkClientTrusted(X509Certificate[] chain, String authType)
        throws CertificateException {
      delegate.checkClientTrusted(chain, authType);
    }

    @Override
    public X509Certificate[] getAcceptedIssuers() {
      return delegate.getAcceptedIssuers();
    }
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
