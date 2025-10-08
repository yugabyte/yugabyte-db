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
public class TelemetryProviderConfig {
  @ApiModelProperty(value = "Telemetry Provider Type", accessMode = AccessMode.READ_WRITE)
  private ProviderType type;

  public void validate(ApiHelper apiHelper) {
    // To be overridden in child classes if validation is required.
    validate(apiHelper, null);
  }

  public void validate(ApiHelper apiHelper, RuntimeConfGetter confGetter) {
    // Default implementation calls the single-parameter version for backward compatibility
    validate(apiHelper, confGetter);
  }
}
