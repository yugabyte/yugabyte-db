package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "DataDog Config")
public class DataDogConfig extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Site", accessMode = READ_WRITE)
  private String site;

  @ApiModelProperty(value = "API Key", accessMode = READ_WRITE)
  private String apiKey;

  public DataDogConfig() {
    setType(ProviderType.DATA_DOG);
  }
}
