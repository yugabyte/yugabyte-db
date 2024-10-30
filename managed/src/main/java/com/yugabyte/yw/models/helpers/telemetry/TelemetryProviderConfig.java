package com.yugabyte.yw.models.helpers.telemetry;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonSubTypes.Type;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
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
  @Type(value = GCPCloudMonitoringConfig.class, name = "GCP_CLOUD_MONITORING")
})
@ApiModel(description = "Telemetry Provider Configuration")
public class TelemetryProviderConfig {
  @ApiModelProperty(value = "Telemetry Provider Type", accessMode = AccessMode.READ_WRITE)
  private ProviderType type;

  public void validate() {
    // To be overridden in child classes if validation is required.
  }
}
