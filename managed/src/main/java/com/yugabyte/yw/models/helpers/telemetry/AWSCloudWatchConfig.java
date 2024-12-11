package com.yugabyte.yw.models.helpers.telemetry;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.data.validation.Constraints;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "AWSCloudWatchConfig Config")
public class AWSCloudWatchConfig extends TelemetryProviderConfig {

  @ApiModelProperty(value = "Log Group", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Log Group is required")
  private String logGroup;

  @ApiModelProperty(value = "Log Stream", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Log Stream is required")
  private String logStream;

  @ApiModelProperty(value = "Region", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Region is required")
  private String region;

  @ApiModelProperty(value = "Role ARN", accessMode = READ_WRITE)
  private String roleARN;

  @ApiModelProperty(value = "End Point", accessMode = READ_WRITE)
  private String endpoint;

  @ApiModelProperty(value = "Access Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Access Key is required")
  private String accessKey;

  @ApiModelProperty(value = "Secret Key", accessMode = READ_WRITE, required = true)
  @Constraints.Required(message = "Secret Key is required")
  private String secretKey;

  public AWSCloudWatchConfig() {
    setType(ProviderType.AWS_CLOUDWATCH);
  }
}
