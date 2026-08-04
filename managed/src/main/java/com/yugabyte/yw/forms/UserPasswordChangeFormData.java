package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

@ApiModel(
    value = "UserPasswordChangeFormData",
    description = "User registration data. The API and UI use this to validate form data.")
@Getter
@Setter
public class UserPasswordChangeFormData {

  @ApiModelProperty(value = "Current Password", example = "Test@1234")
  private String currentPassword;

  @ApiModelProperty(value = "New Password", example = "Test@1234")
  private String newPassword;
}
