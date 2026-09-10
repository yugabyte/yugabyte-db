package com.yugabyte.ByocApiProxy.config;

import static java.nio.charset.StandardCharsets.UTF_8;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.ssl.SslBundles;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

@SpringBootTest
class PollerInfrastructureConfigTest {

  private static final char[] PASSWORD = "changeit".toCharArray();

  @TempDir static Path tempDir;

  private static HttpsServer server;

  @Autowired private HttpClient pollerHttpClient;
  @Autowired private SslBundles sslBundles;

  @DynamicPropertySource
  static void sslBundleProperties(DynamicPropertyRegistry registry) throws Exception {
    Path clientKeystore = tempDir.resolve("client.p12");
    Path serverKeystore = tempDir.resolve("server.p12");
    Path serverCert = tempDir.resolve("server.cer");
    Path truststore = tempDir.resolve("truststore.p12");

    // Client identity used for mTLS to YBA (unused by the one-way TLS test server).
    keytool(
        "-genkeypair",
        "-alias",
        "client",
        "-keyalg",
        "RSA",
        "-keysize",
        "2048",
        "-validity",
        "365",
        "-dname",
        "CN=Test Client",
        "-keystore",
        clientKeystore.toString(),
        "-storetype",
        "PKCS12",
        "-storepass",
        new String(PASSWORD),
        "-keypass",
        new String(PASSWORD));

    // Server cert carries an IP SAN only (mirrors the production cert), and its CN does not match
    // the "localhost" name the client connects with, so hostname verification must fail.
    keytool(
        "-genkeypair",
        "-alias",
        "server",
        "-keyalg",
        "RSA",
        "-keysize",
        "2048",
        "-validity",
        "365",
        "-dname",
        "CN=byoc-test",
        "-ext",
        "san=ip:127.0.0.1",
        "-keystore",
        serverKeystore.toString(),
        "-storetype",
        "PKCS12",
        "-storepass",
        new String(PASSWORD),
        "-keypass",
        new String(PASSWORD));

    keytool(
        "-exportcert",
        "-rfc",
        "-alias",
        "server",
        "-keystore",
        serverKeystore.toString(),
        "-storepass",
        new String(PASSWORD),
        "-file",
        serverCert.toString());

    // Pin the server certificate in the client truststore so chain validation still succeeds.
    keytool(
        "-importcert",
        "-noprompt",
        "-alias",
        "server",
        "-file",
        serverCert.toString(),
        "-keystore",
        truststore.toString(),
        "-storetype",
        "PKCS12",
        "-storepass",
        new String(PASSWORD));

    registry.add("spring.ssl.bundle.jks.yba.keystore.location", () -> "file:" + clientKeystore);
    registry.add("spring.ssl.bundle.jks.yba.keystore.password", () -> "changeit");
    registry.add("spring.ssl.bundle.jks.yba.keystore.type", () -> "PKCS12");
    registry.add("spring.ssl.bundle.jks.yba.truststore.location", () -> "file:" + truststore);
    registry.add("spring.ssl.bundle.jks.yba.truststore.password", () -> "changeit");
    registry.add("spring.ssl.bundle.jks.yba.truststore.type", () -> "PKCS12");

    startServer(serverKeystore);
  }

  private static void startServer(Path serverKeystore) throws Exception {
    KeyStore keyStore = KeyStore.getInstance("PKCS12");
    try (InputStream in = Files.newInputStream(serverKeystore)) {
      keyStore.load(in, PASSWORD);
    }
    KeyManagerFactory kmf = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
    kmf.init(keyStore, PASSWORD);
    SSLContext sslContext = SSLContext.getInstance("TLS");
    sslContext.init(kmf.getKeyManagers(), null, null);

    server = HttpsServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
    server.setHttpsConfigurator(new HttpsConfigurator(sslContext));
    server.createContext(
        "/",
        exchange -> {
          byte[] body = "ok".getBytes(UTF_8);
          exchange.sendResponseHeaders(200, body.length);
          try (OutputStream os = exchange.getResponseBody()) {
            os.write(body);
          }
        });
    server.start();
  }

  @AfterAll
  static void stopServer() {
    if (server != null) {
      server.stop(0);
    }
  }

  private static void keytool(String... args) throws Exception {
    String[] command = new String[args.length + 1];
    command[0] = "keytool";
    System.arraycopy(args, 0, command, 1, args.length);
    ProcessBuilder keytool = new ProcessBuilder(command);
    keytool.redirectErrorStream(true);
    Process process = keytool.start();
    String output = new String(process.getInputStream().readAllBytes(), UTF_8);
    assertThat(process.waitFor()).withFailMessage("keytool failed: %s", output).isZero();
  }

  private URI serverUri() {
    return URI.create("https://localhost:" + server.getAddress().getPort() + "/");
  }

  @Test
  void pollerHttpClient_usesConfiguredSslBundle() {
    assertThat(pollerHttpClient).isNotNull();
  }

  @Test
  void pollerHttpClient_connectsDespiteHostnameMismatch() throws Exception {
    HttpResponse<String> response =
        pollerHttpClient.send(
            HttpRequest.newBuilder(serverUri()).GET().build(),
            HttpResponse.BodyHandlers.ofString());

    assertThat(response.statusCode()).isEqualTo(200);
    assertThat(response.body()).isEqualTo("ok");
  }

  @Test
  void defaultClient_rejectsHostnameMismatch() {
    // Sanity check: the same pinned truststore with standard verification rejects the IP-only
    // certificate presented for "localhost", proving the connect succeeds only because the dispatch
    // client skips hostname identity (not because the cert happens to match).
    HttpClient strictClient =
        HttpClient.newBuilder()
            .sslContext(
                sslBundles
                    .getBundle(PollerInfrastructureConfig.YBA_SSL_BUNDLE_NAME)
                    .createSslContext())
            .build();

    assertThatThrownBy(
            () ->
                strictClient.send(
                    HttpRequest.newBuilder(serverUri()).GET().build(),
                    HttpResponse.BodyHandlers.ofString()))
        .isInstanceOf(IOException.class);
  }
}
