package com.yugabyte.yw.models.helpers.auth;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel("Basic Auth information")
public class BasicAuth extends UsernamePasswordAuth {
  public BasicAuth() {
    setType(HttpAuthType.BASIC);
  }
}
