// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.Min;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
public class CustomerConfigPasswordPolicyData extends CustomerConfigData {
  @ApiModelProperty(value = "Minimal password length", example = "8")
  @Min(value = 1)
  private int minLength = 8;

  @ApiModelProperty(value = "Minimal number of letters in upper case", example = "1")
  @Min(value = 0)
  private int minUppercase = 1;

  @ApiModelProperty(value = "Minimal number of letters in lower case", example = "1")
  @Min(value = 0)
  private int minLowercase = 1;

  @ApiModelProperty(value = "Minimal number of digits", example = "1")
  @Min(value = 0)
  private int minDigits = 1;

  @ApiModelProperty(value = "Minimal number of special characters", example = "1")
  @Min(value = 0)
  private int minSpecialCharacters = 1;
}
