package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.TOO_MANY_REQUESTS;
import static play.mvc.Http.Status.UNAUTHORIZED;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.Base64;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Loki Config")
@Slf4j
public class LokiConfig extends TelemetryProviderConfig {

  // Authenticating endpoint used only to validate credentials; see verifyCredentials.
  private static final String LOKI_LABELS_ENDPOINT = "/loki/api/v1/labels";

  @ApiModelProperty(value = "End Point", accessMode = READ_WRITE, required = true)
  private String endpoint;

  @ApiModelProperty(value = "Auth Type", accessMode = READ_WRITE, required = true)
  private AuthCredentials.AuthType authType;

  @ApiModelProperty(value = "Organization/Tenant ID", accessMode = READ_WRITE)
  private String organizationID;

  @ApiModelProperty(value = "Basic Auth Credentials", accessMode = READ_WRITE)
  private AuthCredentials.BasicAuthCredentials basicAuth;

  public LokiConfig() {
    setType(ProviderType.LOKI);
  }

  @Override
  public void validateConfigFields() {
    if (endpoint == null || endpoint.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki endpoint is required.");
    }

    if (authType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki auth type is required.");
    }

    AuthCredentials.checkBasicAuthCredentials(authType, basicAuth);

    AuthCredentials.bearerTokenNotSupported(authType);

    // The generator concatenates this straight into the exporter's logs_endpoint, so a
    // schemeless value cannot work. No default is safe: http breaks Grafana Cloud, https
    // breaks a plaintext self-hosted Loki.
    if (!endpoint.startsWith("http://") && !endpoint.startsWith("https://")) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Loki endpoint must start with http:// or https://, got: " + endpoint);
    }

    try {
      Util.validateAndGetURL(endpoint, false);
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Loki endpoint: " + e.getMessage());
    }

    if (endpoint.endsWith("/")) {
      endpoint = endpoint.substring(0, endpoint.length() - 1);
    }

    if (endpoint.endsWith(TelemetryProviderService.LOKI_PUSH_ENDPOINT)) {
      // remove the suffix
      endpoint =
          endpoint.substring(
              0, endpoint.length() - TelemetryProviderService.LOKI_PUSH_ENDPOINT.length());
    }
  }

  @VisibleForTesting
  enum ProbeVerdict {
    SERVED,
    PATH_MISSING,
    AUTH_REJECTED,
    UNHEALTHY
  }

  /**
   * The OTLP path is POST-only, so a GET reaching it answers 405, not 200. A 404 is the one
   * definitive "no OTLP endpoint" signal; anything else that is not an auth rejection or a
   * transient server error is treated as served rather than enumerating proxy behaviour.
   */
  @VisibleForTesting
  static ProbeVerdict classifyProbeStatus(int statusCode) {
    if (statusCode == NOT_FOUND) {
      return ProbeVerdict.PATH_MISSING;
    }
    if (statusCode == UNAUTHORIZED || statusCode == FORBIDDEN) {
      return ProbeVerdict.AUTH_REJECTED;
    }
    if (statusCode >= INTERNAL_SERVER_ERROR || statusCode == TOO_MANY_REQUESTS) {
      return ProbeVerdict.UNHEALTHY;
    }
    return ProbeVerdict.SERVED;
  }

  @Override
  public void validateConnectivity(ApiHelper apiHelper) {
    HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(3)).build();

    int maxRetries = 5;
    boolean isReady = false;
    Exception lastException = null;
    int lastStatusCode = -1;

    for (int i = 1; i <= maxRetries; i++) {
      try {
        // Built exactly as the generator builds logs_endpoint: URI.resolve("/...") would drop
        // a base path the exporter keeps, and any divergence here is a false-green config.
        URI readyUri = URI.create(endpoint + TelemetryProviderService.LOKI_OTLP_LOGS_ENDPOINT);

        HttpResponse<String> response =
            client.send(authorizedRequest(readyUri), HttpResponse.BodyHandlers.ofString());

        int statusCode = response.statusCode();
        String body = response.body();

        lastStatusCode = statusCode;

        if (classifyProbeStatus(statusCode) == ProbeVerdict.SERVED) {
          isReady = true;
          break;
        }

        log.warn(
            "Loki OTLP endpoint not usable yet (attempt {} of {}). Status: {}, Body: {}",
            i,
            maxRetries,
            statusCode,
            body);
      } catch (Exception e) {
        lastException = e;
        log.warn(
            "Error checking Loki readiness (attempt {} of {}): {}", i, maxRetries, e.getMessage());
      }

      try {
        Thread.sleep(1000);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Loki validation thread interrupted during retry delay: " + e.getMessage());
      }
    }

    if (!isReady) {
      StringBuilder errorMsg = new StringBuilder();
      if (lastStatusCode == NOT_FOUND) {
        errorMsg
            .append("Loki does not serve ")
            .append(TelemetryProviderService.LOKI_OTLP_LOGS_ENDPOINT)
            .append(" (HTTP 404). OTLP log ingestion requires Loki 3.0 or newer; against an")
            .append(" older Loki the collector accepts logs and then drops all of them.");
      } else if (lastStatusCode == UNAUTHORIZED || lastStatusCode == FORBIDDEN) {
        errorMsg
            .append("Loki rejected the supplied credentials (HTTP ")
            .append(lastStatusCode)
            .append(").");
      } else {
        errorMsg
            .append("Loki endpoint is not ready after ")
            .append(maxRetries)
            .append(" attempts.");
        if (lastStatusCode != -1) {
          errorMsg.append(" Last status code: ").append(lastStatusCode);
        }
      }
      if (lastException != null) {
        errorMsg.append(" Error: ").append(lastException.getMessage());
      }

      throw new PlatformServiceException(BAD_REQUEST, errorMsg.toString());
    }

    verifyCredentials(client);
  }

  /**
   * Rejects bad credentials, which the OTLP probe alone cannot see: Grafana Cloud answers
   * /otlp/v1/logs with 405 before it authenticates, so a served OTLP path says nothing about
   * whether the credentials work. The labels endpoint does authenticate. Only an explicit auth
   * failure is fatal here - any other status (including a 404 on some future Loki that drops this
   * endpoint) is not a credential problem and must not fail validation.
   */
  private void verifyCredentials(HttpClient client) {
    int statusCode;
    try {
      HttpResponse<String> response =
          client.send(
              authorizedRequest(URI.create(endpoint + LOKI_LABELS_ENDPOINT)),
              HttpResponse.BodyHandlers.ofString());
      statusCode = response.statusCode();
    } catch (Exception e) {
      // Unreachable or timed out: the OTLP probe already proved the host is reachable, so this
      // is not evidence about credentials.
      log.warn("Could not verify Loki credentials: {}", e.getMessage());
      return;
    }
    if (classifyProbeStatus(statusCode) == ProbeVerdict.AUTH_REJECTED) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Loki rejected the supplied credentials (HTTP %d on %s).",
              statusCode, LOKI_LABELS_ENDPOINT));
    }
  }

  /** A GET carrying the same auth and tenant headers the collector's exporter sends. */
  private HttpRequest authorizedRequest(URI uri) {
    HttpRequest.Builder builder =
        HttpRequest.newBuilder().uri(uri).timeout(Duration.ofSeconds(3)).GET();
    // Without the tenant header a multi-tenant Loki's rejection looks like a missing path.
    if (organizationID != null && !organizationID.isEmpty()) {
      builder.header("X-Scope-OrgID", organizationID);
    }
    if (authType == AuthCredentials.AuthType.BasicAuth) {
      builder.header(
          "Authorization",
          "Basic "
              + Base64.getEncoder()
                  .encodeToString(
                      (basicAuth.getUsername() + ":" + basicAuth.getPassword()).getBytes()));
    }
    return builder.build();
  }
}
