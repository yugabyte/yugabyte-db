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
import org.apache.commons.lang3.StringUtils;

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
              + " setting is ignored for logs. Allowed only for HTTP protocol",
      accessMode = READ_WRITE)
  private String logsEndpoint;

  @ApiModelProperty(
      value =
          "Metrics endpoint. The target URL to send metric data to (e.g.:"
              + " https://example.com:4318/v1/metrics). If this setting is present the endpoint"
              + " setting is ignored for metrics. Allowed only for HTTP protocol",
      accessMode = READ_WRITE)
  private String metricsEndpoint;

  @ApiModelProperty(
      value =
          "Optional retry-on-failure config for the Otel collector exporter. When set, overrides "
              + "default retry settings (initial_interval, max_interval, max_elapsed_time). "
              + "Duration format: e.g. \"30s\", \"1m\", \"60m\".",
      accessMode = READ_WRITE)
  private ExporterRetryConfig retryOnFailure;

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

    if (StringUtils.isNotBlank(logsEndpoint) && !Protocol.HTTP.equals(protocol)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "logsEndpoint is allowed only for HTTP protocol.");
    }
    if (StringUtils.isNotBlank(metricsEndpoint) && !Protocol.HTTP.equals(protocol)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "metricsEndpoint is allowed only for HTTP protocol.");
    }
  }
}
