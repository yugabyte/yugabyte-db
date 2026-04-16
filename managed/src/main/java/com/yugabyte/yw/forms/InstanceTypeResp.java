package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.InstanceType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel(description = "Details of a cloud instance type")
public class InstanceTypeResp {
  @JsonUnwrapped
  @JsonIgnoreProperties({"idKey", "provider"})
  @ApiModelProperty(value = "Instance type", accessMode = READ_ONLY)
  private InstanceType instanceType;

  @ApiModelProperty(value = "Cloud provider code", accessMode = READ_ONLY)
  private String providerCode;

  @ApiModelProperty(value = "Provider UUID", accessMode = READ_ONLY)
  private UUID providerUuid;
}
