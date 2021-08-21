// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import static com.yugabyte.yw.models.Users.Role;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
@ApiModel(value = "UserRegistrationData", description = "User registration data")
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

  @Constraints.Required()
  @ApiModelProperty(value = "User role", example = "Admin", required = true)
  private Role role;

  public String getEmail() {
    return email;
  }

  public void setEmail(String email) {
    this.email = email;
  }

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

  public Map getFeatures() {
    return features;
  }

  public void setFeatures(Map features) {
    this.features = features;
  }

  public Role getRole() {
    return role;
  }

  public void setRole(Role role) {
    this.role = role;
  }
}
