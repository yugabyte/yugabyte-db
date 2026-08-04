package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

/**
 * Container class for authentication credentials used in telemetry providers. Contains both Basic
 * Auth and Bearer Token authentication types.
 */
public class AuthCredentials {

  public enum AuthType {
    BasicAuth("BasicAuth"),
    NoAuth("NoAuth"),
    BearerToken("BearerToken");

    private final String authType;

    AuthType(String authType) {
      this.authType = authType;
    }

    @Override
    public String toString() {
      return this.name();
    }

    public static AuthType fromString(String input) {
      for (AuthType authType : AuthType.values()) {
        if (authType.authType.equalsIgnoreCase(input)) {
          return authType;
        }
      }
      throw new IllegalArgumentException(
          "No enum constant " + AuthType.class.getName() + "." + input);
    }
  }

  @Data
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

    @Override
    public String toString() {
      return "BasicAuthCredentials{" + "username='" + username + '\'' + ", password='******'" + '}';
    }
  }

  @Data
  public static class BearerToken {
    @ApiModelProperty(value = "Bearer Token", accessMode = READ_WRITE, required = true)
    private String token;

    @JsonIgnore
    @ApiModelProperty(hidden = true)
    public boolean isEmpty() {
      return token == null || token.isEmpty();
    }

    public BearerToken() {}

    public BearerToken(String token) {
      this.token = token;
    }

    @Override
    public String toString() {
      return "BearerToken{" + "token='******'" + '}';
    }
  }

  public static void checkBasicAuthCredentials(AuthType authType, BasicAuthCredentials basicAuth) {
    if (authType == AuthType.BasicAuth && (basicAuth == null || basicAuth.isEmpty())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Credentials are required when auth type is basic.");
    }

    if (authType != AuthType.BasicAuth && basicAuth != null && !basicAuth.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Basic auth credentials are not required.");
    }
  }

  public static void checkBearerToken(AuthType authType, BearerToken bearerToken) {
    if (authType == AuthType.BearerToken && (bearerToken == null || bearerToken.isEmpty())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Bearer token is required when auth type is bearer token.");
    }

    if (authType != AuthType.BearerToken && bearerToken != null && !bearerToken.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Bearer token is not required.");
    }
  }

  public static void basicAuthCredentialsNotSupported(AuthType authType) {
    if (authType == AuthType.BasicAuth) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Basic auth credentials are not supported for this provider.");
    }
  }

  public static void bearerTokenNotSupported(AuthType authType) {
    if (authType == AuthType.BearerToken) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Bearer token is not supported for this provider.");
    }
  }
}
