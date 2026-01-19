package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "OTLP Config")
@Slf4j
public class OTLPConfig extends TelemetryProviderConfig {

  @ApiModelProperty(
      value = "End Point. For HTTP protcol logs export \"/v1/logs\" will be appended.",
      accessMode = READ_WRITE,
      required = true)
  private String endpoint;

  @ApiModelProperty(value = "Auth Type", accessMode = READ_WRITE, required = true)
  private AuthCredentials.AuthType authType;

  @ApiModelProperty(value = "Basic Auth Credentials", accessMode = READ_WRITE)
  private AuthCredentials.BasicAuthCredentials basicAuth;

  @ApiModelProperty(value = "Bearer Token", accessMode = READ_WRITE)
  private AuthCredentials.BearerToken bearerToken;

  @ApiModelProperty(value = "Headers", accessMode = READ_WRITE)
  private Map<String, String> headers;

  @ApiModelProperty(value = "Compression", accessMode = READ_WRITE)
  private CompressionType compression = CompressionType.gzip;

  @ApiModelProperty(value = "Timeout in seconds", accessMode = READ_WRITE)
  private Integer timeoutSeconds = 5;

  @ApiModelProperty(value = "Protocol", accessMode = READ_WRITE)
  private Protocol protocol = Protocol.gRPC;

  @ApiModelProperty(
      value =
          "Logs endpoint. The target URL to send log data to (e.g.:"
              + " https://example.com:4318/v1/logs). If this setting is present the endpoint"
              + " setting is ignored logs. Allowed only for HTTP protocol",
      accessMode = READ_WRITE)
  private String logsEndpoint;

  public enum Protocol {
    gRPC("otlp"),
    HTTP("otlphttp");

    private final String exporterType;

    Protocol(String exporterType) {
      this.exporterType = exporterType;
    }

    public String getExporterType() {
      return this.exporterType;
    }

    @Override
    public String toString() {
      return this.name();
    }
  }

  public OTLPConfig() {
    setType(ProviderType.OTLP);
  }

  @Override
  public void validateConfigFields() {
    if (endpoint == null || endpoint.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Endpoint is required.");
    }

    if (timeoutSeconds == null || timeoutSeconds <= 0) {
      throw new PlatformServiceException(BAD_REQUEST, "timeoutSeconds must be positive.");
    }

    if (authType == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Auth type is required.");
    }

    AuthCredentials.checkBasicAuthCredentials(authType, basicAuth);

    AuthCredentials.checkBearerToken(authType, bearerToken);

    if (logsEndpoint != null && !logsEndpoint.isEmpty()) {
      if (protocol != Protocol.HTTP) {
        throw new PlatformServiceException(
            BAD_REQUEST, "logsEndpoint is allowed only for HTTP protocol.");
      }
    }
  }
}
