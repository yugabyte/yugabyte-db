package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
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
  public void validate() {

    if (endpoint == null || endpoint.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki endpoint is required.");
    }

    if (authType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Loki auth type is required.");
    }

    if (authType == LokiAuthType.BasicAuth && (basicAuth == null || basicAuth.isEmpty())) {
      throw new PlatformServiceException(BAD_REQUEST, "Basic auth credentials are required.");
    }

    if (authType != LokiAuthType.BasicAuth && basicAuth != null && !basicAuth.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Basic auth credentials are not required.");
    }

    log.info("Successfully validated Loki config.");
  }
}
