package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Users;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
@ApiModel(
    value = "UserProfileData",
    description = "User profile data. The API and UI use this to validate form data.")
public class UserProfileFormData {

  @ApiModelProperty(value = "Password", example = "Test@1234")
  private String password;

  @ApiModelProperty(value = "Password confirmation", example = "Test@1234")
  private String confirmPassword;

  @Constraints.Required()
  @ApiModelProperty(value = "User role", example = "Admin", required = true)
  private Users.Role role;

  @ApiModelProperty(value = "User timezone", example = "America/Toronto")
  private String timezone;

  public String getPassword() {
    return password;
  }

  public void setPassword(String password) {
    this.password = password;
  }

  public String getConfirmPassword() {
    return confirmPassword;
  }

  public void setConfirmPassword(String confirmPassword) {
    this.confirmPassword = confirmPassword;
  }

  public Users.Role getRole() {
    return role;
  }

  public void setRole(Users.Role role) {
    this.role = role;
  }

  public String getTimezone() {
    return timezone;
  }

  public void setTimezone(String timezone) {
    this.timezone = timezone;
  }
}
