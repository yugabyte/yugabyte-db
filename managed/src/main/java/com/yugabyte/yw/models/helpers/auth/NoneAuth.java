package com.yugabyte.yw.models.helpers.auth;

import io.swagger.annotations.ApiModel;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel("No Auth")
public class NoneAuth extends HttpAuth {
  public NoneAuth() {
    setType(HttpAuthType.NONE);
  }
}
