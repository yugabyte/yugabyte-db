package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.databind.JsonNode;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "GCPCloudMonitoringConfig Config")
public class GCPCloudMonitoringConfig extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Project", accessMode = READ_WRITE)
  private String project;

  @ApiModelProperty(value = "Credntials", accessMode = READ_WRITE)
  private JsonNode credentials;

  public GCPCloudMonitoringConfig() {
    setType(ProviderType.GCP_CLOUD_MONITORING);
  }
}
