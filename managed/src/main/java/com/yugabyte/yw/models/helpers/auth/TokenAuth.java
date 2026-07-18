package com.yugabyte.yw.models.helpers.auth;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel("Token Auth information")
public class TokenAuth extends HttpAuth {
  public TokenAuth() {
    setType(HttpAuthType.TOKEN);
  }

  @ApiModelProperty(value = "Header name", accessMode = AccessMode.READ_WRITE)
  private String tokenHeader;

  @ApiModelProperty(value = "Token value", accessMode = AccessMode.READ_WRITE)
  private String tokenValue;
}
