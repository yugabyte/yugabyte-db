package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Splunk Config")
public class SplunkConfig extends TelemetryProviderConfig {
  @ApiModelProperty(value = "End Point", accessMode = READ_WRITE)
  private String endpoint;

  @ApiModelProperty(value = "Token", accessMode = READ_WRITE)
  private String token;

  @ApiModelProperty(value = "Source", accessMode = READ_WRITE)
  private String source;

  @ApiModelProperty(value = "Source Type", accessMode = READ_WRITE)
  private String sourceType;

  @ApiModelProperty(value = "Index", accessMode = READ_WRITE)
  private String index;

  public SplunkConfig() {
    setType(ProviderType.SPLUNK);
  }
}
