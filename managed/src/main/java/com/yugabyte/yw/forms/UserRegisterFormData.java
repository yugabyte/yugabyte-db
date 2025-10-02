// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static com.yugabyte.yw.models.Users.Role;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.common.rbac.RoleResourceDefinition;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Map;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
@ApiModel(
    value = "UserRegistrationData",
    description = "User registration data. The API and UI use this to validate form data.")
@Getter
@Setter
public class UserRegisterFormData {
  @Constraints.Required()
  @ApiModelProperty(value = "Email address", example = "test@example.com", required = true)
  @Constraints.Email
  private String email;

  @ApiModelProperty(value = "Password", example = "Test@1234")
  private String password;

  @ApiModelProperty(value = "Password confirmation", example = "Test@1234")
  private String confirmPassword;

  @ApiModelProperty(value = "User features")
  private Map features;

  @ApiModelProperty(
      value =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2.19.3.0.</b> Use field"
              + " roleResourceDefinitions instead.",
      example = "Admin")
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2.19.3.0")
  private Role role;

  @ApiModelProperty(value = "List of roles and resource groups defined for user.")
  @JsonProperty("roleResourceDefinitions")
  private List<RoleResourceDefinition> roleResourceDefinitions;

  @ApiModelProperty(value = "User timezone", example = "America/Toronto")
  private String timezone;
}
