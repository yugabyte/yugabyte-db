package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.net.URI;
import java.net.URL;
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

  public enum LokiAuthType {
    BasicAuth("BasicAuth"),
    NoAuth("NoAuth");

    private final String state;

    LokiAuthType(String state) {
      this.state = state;
    }

    @Override
    public String toString() {
      return this.name();
    }

    public String getState() {
      return this.state;
    }

    public static LokiAuthType fromString(String input) {
      for (LokiAuthType state : LokiAuthType.values()) {
        if (state.state.equalsIgnoreCase(input)) {
          return state;
        }
      }
      throw new IllegalArgumentException(
          "No enum constant " + LokiAuthType.class.getName() + "." + input);
    }
  }

  public static class BasicAuthCredentials {
    @ApiModelProperty(value = "Username", accessMode = READ_WRITE, required = true)
    private String username;

    @ApiModelProperty(value = "Password", accessMode = READ_WRITE, required = true)
    private String password;

    @JsonIgnore
    @ApiModelProperty(hidden = true)
    public boolean isEmpty() {
      // Check if username or password is null or empty
      return username == null || username.isEmpty() || password == null || password.isEmpty();
    }

    public BasicAuthCredentials() {}

    public BasicAuthCredentials(String username, String password) {
      this.username = username;
      this.password = password;
    }

    public String getUsername() {
      return username;
    }

    public String getPassword() {
      return password;
    }

    @Override
    public String toString() {
      return "BasicAuthCredentials{" + "username='" + username + '\'' + ", password='******'" + '}';
    }
  }

  @ApiModelProperty(value = "End Point", accessMode = READ_WRITE, required = true)
  private String endpoint;

  @ApiModelProperty(value = "Auth Type", accessMode = READ_WRITE, required = true)
  private LokiAuthType authType;

  @ApiModelProperty(value = "Organization/Tenant ID", accessMode = READ_WRITE)
  private String organizationID;

  @ApiModelProperty(value = "Basic Auth Credentials", accessMode = READ_WRITE)
  private BasicAuthCredentials basicAuth;

  public LokiConfig() {
    setType(ProviderType.LOKI);
  }

  @Override
  public void validate(ApiHelper apiHelper) {

    if (endpoint == null || endpoint.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki endpoint is required.");
    }

    if (authType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki auth type is required.");
    }

    if (authType == LokiAuthType.BasicAuth && (basicAuth == null || basicAuth.isEmpty())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Credentials are required when auth type is basic.");
    }

    if (authType != LokiAuthType.BasicAuth && basicAuth != null && !basicAuth.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Basic auth credentials are not required.");
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

    HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(3)).build();

    int maxRetries = 5;
    boolean isReady = false;
    Exception lastException = null;
    int lastStatusCode = -1;

    for (int i = 1; i <= maxRetries; i++) {
      try {
        URL validatedBaseUrl = Util.validateAndGetURL(endpoint, true);
        URI readyUri = validatedBaseUrl.toURI().resolve("/ready");

        HttpRequest.Builder requestBuilder =
            HttpRequest.newBuilder().uri(readyUri).timeout(Duration.ofSeconds(3)).GET();

        if (authType == LokiAuthType.BasicAuth) {
          String authHeader =
              "Basic "
                  + Base64.getEncoder()
                      .encodeToString(
                          (basicAuth.getUsername() + ":" + basicAuth.getPassword()).getBytes());
          requestBuilder.header("Authorization", authHeader);
        }

        HttpRequest request = requestBuilder.build();
        HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

        int statusCode = response.statusCode();
        String body = response.body();

        lastStatusCode = statusCode;

        if (statusCode == 200 && "Ready".equalsIgnoreCase(body.trim())) {
          isReady = true;
          break;
        }

        log.warn(
            "Loki not ready yet (attempt {} of {}). Status: {}, Body: {}",
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
      StringBuilder errorMsg =
          new StringBuilder("Loki endpoint is not ready after " + maxRetries + " attempts.");
      if (lastStatusCode != -1) {
        errorMsg.append(" Last status code: ").append(lastStatusCode);
      }
      if (lastException != null) {
        errorMsg.append(", error: ").append(lastException.getMessage());
      }

      throw new PlatformServiceException(BAD_REQUEST, errorMsg.toString());
    }

    log.info("Successfully validated Loki config.");
  }
}
