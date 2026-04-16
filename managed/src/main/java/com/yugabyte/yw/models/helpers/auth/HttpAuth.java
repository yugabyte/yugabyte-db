package com.yugabyte.yw.models.helpers.auth;

import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonSubTypes.Type;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Data;

@Data
@JsonTypeInfo(use = JsonTypeInfo.Id.NAME, property = "type")
@JsonSubTypes({
  @Type(value = NoneAuth.class, name = "NONE"),
  @Type(value = BasicAuth.class, name = "BASIC"),
  @Type(value = TokenAuth.class, name = "TOKEN")
})
@ApiModel("HTTP Auth information")
public class HttpAuth {
  @ApiModelProperty(value = "HTTP Auth type", accessMode = AccessMode.READ_WRITE)
  private HttpAuthType type;
}
