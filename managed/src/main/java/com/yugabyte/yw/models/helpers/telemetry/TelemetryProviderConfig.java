package com.yugabyte.yw.models.helpers.telemetry;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonSubTypes.Type;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;

@Data
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, property = "type")
@JsonSubTypes({
  @Type(value = DataDogConfig.class, name = "DATA_DOG"),
  @Type(value = SplunkConfig.class, name = "SPLUNK"),
  @Type(value = AWSCloudWatchConfig.class, name = "AWS_CLOUDWATCH"),
  @Type(value = GCPCloudMonitoringConfig.class, name = "GCP_CLOUD_MONITORING"),
  @Type(value = LokiConfig.class, name = "LOKI"),
  @Type(value = DynatraceConfig.class, name = "DYNATRACE"),
  @Type(value = S3Config.class, name = "S3"),
  @Type(value = OTLPConfig.class, name = "OTLP")
})
@ApiModel(description = "Telemetry Provider Configuration")
@Slf4j
public class TelemetryProviderConfig {
  @ApiModelProperty(value = "Telemetry Provider Type", accessMode = AccessMode.READ_WRITE)
  private ProviderType type;

  /**
   * Template method for validation. First validates config fields, then checks if connectivity
   * validation should be skipped, and finally validates connectivity if not skipped.
   */
  public final void validate(ApiHelper apiHelper, RuntimeConfGetter confGetter) {
    // First, validate config fields (always runs)
    validateConfigFields();
    log.debug("Successfully validated {} config fields.", getType());

    // Check if connectivity validation should be skipped
    if (TelemetryProviderUtil.skipConnectivityValidation(confGetter)) {
      log.info("Skipping {} connectivity validation as per config.", getType());
      return;
    }

    // Validate connectivity
    validateConnectivity(apiHelper);
    log.debug("Successfully validated {} connectivity.", getType());
  }

  /**
   * Override this method to validate config fields (required fields, format, etc.). This validation
   * always runs regardless of the "yb.telemetry.skip_connectivity_validations" runtime flag.
   */
  public void validateConfigFields() {
    // Child classes should override as needed.
  }

  /**
   * Override this method to validate connectivity (API calls, credential checks, etc.). This
   * validation is skipped if the "yb.telemetry.skip_connectivity_validations" runtime flag is set.
   */
  public void validateConnectivity(ApiHelper apiHelper) {
    // Child classes should override as needed.
  }
}
