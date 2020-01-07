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
  @Constraints.MinLength(5)
  public String email;

  @Constraints.MinLength(6)
  public String password;

  @Constraints.MinLength(6)
  public String confirmPassword;

  public Map features;

  @Constraints.Required()
  public Role role;
}
