package com.yugabyte.yw.models.helpers.auth;

import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
public class UsernamePasswordAuth extends HttpAuth {

  @ApiModelProperty(value = "Username", accessMode = AccessMode.READ_WRITE)
  private String username;

  @ApiModelProperty(value = "Password", accessMode = AccessMode.READ_WRITE)
  private String password;
}
