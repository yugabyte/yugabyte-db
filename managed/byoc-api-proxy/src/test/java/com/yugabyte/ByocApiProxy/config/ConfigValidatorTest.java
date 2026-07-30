// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.ByocApiProxy.config;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import org.springframework.boot.context.properties.source.ConfigurationPropertySources;
import org.springframework.core.env.MapPropertySource;
import org.springframework.mock.env.MockEnvironment;

class ConfigValidatorTest {

  @Test
  void collectErrors_validServiceAccountConfig_returnsEmpty() {
    MockEnvironment env =
        baseEnv()
            .withProperty("proxied-app.auth.type", "service_account")
            .withProperty("proxied-app.auth.service-account.email", "admin@example.com")
            .withProperty("proxied-app.auth.service-account.password", "secret")
            .withProperty("proxied-app.auth.service-account.refresh-interval", "PT10H");

    assertTrue(ConfigValidator.collectErrors(env).isEmpty());
  }

  @Test
  void collectErrors_validApiKeyConfig_returnsEmpty() {
    MockEnvironment env =
        baseEnv()
            .withProperty("proxied-app.auth.type", "api_key")
            .withProperty("proxied-app.auth.api-key", "tok-123");

    assertTrue(ConfigValidator.collectErrors(env).isEmpty());
  }

  @Test
  void collectErrors_aggregatesMultipleIndependentProblems() {
    MockEnvironment env =
        new MockEnvironment()
            // yba.uuid missing / blank -> bind or validation error
            .withProperty("yba.base-url", "")
            // proxied-app.base-url blank, bad poll batch size, missing auth material
            .withProperty("proxied-app.base-url", "")
            .withProperty("proxied-app.read-timeout", "PT30S")
            .withProperty("proxied-app.poll-batch-size", "0")
            .withProperty("proxied-app.auth.type", "service_account");

    List<String> errors = ConfigValidator.collectErrors(env);

    assertFalse(errors.isEmpty());
    assertTrue(
        errors.stream().anyMatch(e -> e.startsWith("yba.")), "expected yba errors, got: " + errors);
    assertTrue(
        errors.stream().anyMatch(e -> e.contains("proxied-app.baseUrl") || e.contains("baseUrl")),
        "expected proxied-app.baseUrl error, got: " + errors);
    assertTrue(
        errors.stream().anyMatch(e -> e.contains("pollBatchSize")),
        "expected pollBatchSize error, got: " + errors);
    assertTrue(
        errors.stream().anyMatch(e -> e.contains("serviceAccount")),
        "expected serviceAccount cross-check error, got: " + errors);
  }

  @Test
  void collectErrors_apiKeyTypeWithoutKey_reportsAuthCrossCheck() {
    MockEnvironment env = baseEnv().withProperty("proxied-app.auth.type", "api_key");
    // api-key intentionally omitted

    List<String> errors = ConfigValidator.collectErrors(env);

    assertTrue(
        errors.stream().anyMatch(e -> e.contains("apiKey") && e.contains("api_key")),
        "expected apiKey cross-check error, got: " + errors);
  }

  @Test
  void collectErrors_serviceAccountNestedBlankFields_reportedTogether() {
    MockEnvironment env =
        baseEnv()
            .withProperty("proxied-app.auth.type", "service_account")
            .withProperty("proxied-app.auth.service-account.email", "")
            .withProperty("proxied-app.auth.service-account.password", "")
            .withProperty("proxied-app.auth.service-account.refresh-interval", "PT10H");

    List<String> errors = ConfigValidator.collectErrors(env);

    assertTrue(
        errors.stream().anyMatch(e -> e.contains("serviceAccount.email")),
        "expected email violation, got: " + errors);
    assertTrue(
        errors.stream().anyMatch(e -> e.contains("serviceAccount.password")),
        "expected password violation, got: " + errors);
  }

  @Test
  void validateAndReport_printsAllErrorsAndReturnsOne() {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    ByteArrayOutputStream err = new ByteArrayOutputStream();

    int code =
        ConfigValidator.validateAndReport(
            new String[] {
              ConfigValidator.FLAG,
              "--yba.uuid=",
              "--yba.base-url=",
              "--proxied-app.base-url=",
              "--proxied-app.read-timeout=PT30S",
              "--proxied-app.auth.type=service_account"
            },
            new PrintStream(out, true, StandardCharsets.UTF_8),
            new PrintStream(err, true, StandardCharsets.UTF_8));

    assertEquals(1, code);
    String errText = err.toString(StandardCharsets.UTF_8);
    assertTrue(errText.contains("Configuration validation failed"), errText);
    assertTrue(errText.contains("error(s)"), errText);
  }

  @Test
  void collectErrors_unresolvedPlaceholders_areReportedClearly() {
    MockEnvironment env =
        new MockEnvironment()
            .withProperty("yba.uuid", "${YBA_UUID}")
            .withProperty("yba.base-url", "http://localhost:9001/api")
            .withProperty("proxied-app.base-url", "http://localhost:9000/api")
            .withProperty("proxied-app.read-timeout", "PT30S")
            .withProperty("proxied-app.auth.type", "service_account")
            .withProperty("proxied-app.auth.service-account.email", "${SERVICE_ACCOUNT_EMAIL}")
            .withProperty(
                "proxied-app.auth.service-account.password", "${SERVICE_ACCOUNT_PASSWORD}")
            .withProperty("proxied-app.auth.service-account.refresh-interval", "PT10H")
            // Present in application.yaml but unused for service_account auth.
            .withProperty("proxied-app.auth.api-key", "${API_KEY}");

    List<String> errors = ConfigValidator.collectErrors(env);

    assertTrue(
        errors.stream()
            .anyMatch(
                e -> e.contains("yba.uuid") && e.contains("YBA_UUID") && e.contains("not set")),
        "expected clear YBA_UUID message, got: " + errors);
    assertTrue(
        errors.stream().anyMatch(e -> e.contains("SERVICE_ACCOUNT_EMAIL") && e.contains("not set")),
        "expected clear SERVICE_ACCOUNT_EMAIL message, got: " + errors);
    assertTrue(
        errors.stream()
            .anyMatch(e -> e.contains("SERVICE_ACCOUNT_PASSWORD") && e.contains("not set")),
        "expected clear SERVICE_ACCOUNT_PASSWORD message, got: " + errors);
    assertTrue(
        errors.stream().noneMatch(e -> e.contains("API_KEY")),
        "api_key is optional for service_account auth, got: " + errors);
    assertTrue(
        errors.stream().noneMatch(e -> e.contains("Failed to convert")),
        "should not surface raw conversion errors, got: " + errors);
  }

  @Test
  void collectErrors_placeholdersShadowedByOverlay_notReported() {
    // Bundled application.yaml style defaults with required placeholders.
    MockEnvironment env =
        new MockEnvironment()
            .withProperty("yba.uuid", "${YBA_UUID}")
            .withProperty("yba.base-url", "${YBA_BASE_URL:http://localhost:9001/api}")
            .withProperty("proxied-app.base-url", "${PROXIED_APP_BASE_URL:http://localhost:9000}")
            .withProperty("proxied-app.read-timeout", "PT30S")
            .withProperty("proxied-app.auth.type", "${PROXIED_APP_AUTH_TYPE:service_account}")
            .withProperty("proxied-app.auth.api-key", "${API_KEY}");
    // Installer-generated application.yaml overlay (snake_case keys, higher precedence).
    env.getPropertySources()
        .addFirst(
            new MapPropertySource(
                "overlay",
                Map.of(
                    "yba.uuid", UUID.randomUUID().toString(),
                    "proxied_app.auth.type", "api_key",
                    "proxied_app.auth.api_key", "tok-123")));
    // Match a Boot-loaded environment, where relaxed property resolution is attached.
    ConfigurationPropertySources.attach(env);

    List<String> errors = ConfigValidator.collectErrors(env);

    assertTrue(errors.isEmpty(), "expected no errors, got: " + errors);
  }

  @Test
  void collectErrors_unresolvedServiceAccountPlaceholders_ignoredForApiKeyAuth() {
    MockEnvironment env =
        baseEnv()
            .withProperty("proxied-app.auth.type", "api_key")
            .withProperty("proxied-app.auth.api-key", "${API_KEY}")
            .withProperty("proxied-app.auth.service-account.email", "${SERVICE_ACCOUNT_EMAIL}")
            .withProperty(
                "proxied-app.auth.service-account.password", "${SERVICE_ACCOUNT_PASSWORD}")
            .withProperty("proxied-app.auth.service-account.refresh-interval", "PT10H");

    List<String> errors = ConfigValidator.collectErrors(env);

    assertTrue(
        errors.stream().anyMatch(e -> e.contains("API_KEY") && e.contains("not set")),
        "expected API_KEY message, got: " + errors);
    assertTrue(
        errors.stream().noneMatch(e -> e.contains("SERVICE_ACCOUNT_EMAIL")),
        "service_account is optional for api_key auth, got: " + errors);
    assertTrue(
        errors.stream().noneMatch(e -> e.contains("SERVICE_ACCOUNT_PASSWORD")),
        "service_account is optional for api_key auth, got: " + errors);
  }

  @Test
  void isValidateConfigRequest_detectsFlag() {
    assertTrue(ConfigValidator.isValidateConfigRequest(new String[] {ConfigValidator.FLAG}));
    assertTrue(
        ConfigValidator.isValidateConfigRequest(
            new String[] {"--spring.config.location=file:x", ConfigValidator.FLAG}));
    assertFalse(ConfigValidator.isValidateConfigRequest(new String[] {}));
    assertFalse(ConfigValidator.isValidateConfigRequest(null));
  }

  private static MockEnvironment baseEnv() {
    return new MockEnvironment()
        .withProperty("yba.uuid", UUID.randomUUID().toString())
        .withProperty("yba.base-url", "http://localhost:9001/api")
        .withProperty("proxied-app.base-url", "http://localhost:9000/api")
        .withProperty("proxied-app.read-timeout", "PT30S")
        .withProperty("proxied-app.poll-batch-size", "10");
  }
}
