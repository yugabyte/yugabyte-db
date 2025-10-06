// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
public class CustomerLoginFormData {
  @Constraints.Required()
  @Constraints.MinLength(5)
  private String email;

  @Constraints.Required() private String password;

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
}
