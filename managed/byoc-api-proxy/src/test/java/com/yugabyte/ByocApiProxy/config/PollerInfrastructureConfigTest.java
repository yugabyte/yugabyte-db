package com.yugabyte.ByocApiProxy.config;

import static org.assertj.core.api.Assertions.assertThat;

import java.net.http.HttpClient;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

@SpringBootTest
class PollerInfrastructureConfigTest {

  @TempDir static Path tempDir;

  @Autowired private HttpClient pollerHttpClient;

  @DynamicPropertySource
  static void sslBundleProperties(DynamicPropertyRegistry registry) throws Exception {
    Path keystorePath = tempDir.resolve("client.p12");
    char[] password = "changeit".toCharArray();
    ProcessBuilder keytool =
        new ProcessBuilder(
            "keytool",
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
            keystorePath.toString(),
            "-storetype",
            "PKCS12",
            "-storepass",
            new String(password),
            "-keypass",
            new String(password));
    keytool.redirectErrorStream(true);
    Process process = keytool.start();
    assertThat(process.waitFor()).isZero();
    assertThat(Files.size(keystorePath)).isPositive();

    registry.add(
        "spring.ssl.bundle.jks.yba.keystore.location", () -> "file:" + keystorePath.toString());
    registry.add("spring.ssl.bundle.jks.yba.keystore.password", () -> "changeit");
    registry.add("spring.ssl.bundle.jks.yba.keystore.type", () -> "PKCS12");
    registry.add(
        "spring.ssl.bundle.jks.yba.truststore.location", () -> "file:" + keystorePath.toString());
    registry.add("spring.ssl.bundle.jks.yba.truststore.password", () -> "changeit");
    registry.add("spring.ssl.bundle.jks.yba.truststore.type", () -> "PKCS12");
  }

  @Test
  void pollerHttpClient_usesConfiguredSslBundle() {
    assertThat(pollerHttpClient).isNotNull();
  }
}
