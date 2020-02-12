// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;


public class DatabaseUserFormData {

  public String ycqlAdminUsername;
  public String ycqlAdminPassword;

  public String ysqlAdminUsername;
  public String ysqlAdminPassword;
  public String dbName;

  @Constraints.Required()
  public String username;

  @Constraints.Required()
  public String password;
}
