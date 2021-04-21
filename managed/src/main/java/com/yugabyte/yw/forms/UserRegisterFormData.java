// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;


import play.data.validation.Constraints;
import java.util.Map;

import static com.yugabyte.yw.models.Users.Role;


/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class UserRegisterFormData {
  @Constraints.Required()
  @Constraints.Email
  private String email;

  private String password;

  private String confirmPassword;

  private Map features;

  @Constraints.Required()
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
