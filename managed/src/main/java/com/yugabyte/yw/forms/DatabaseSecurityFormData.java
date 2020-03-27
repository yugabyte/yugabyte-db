// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;


public class DatabaseSecurityFormData {

  public String ycqlAdminUsername;
  public String ycqlCurrAdminPassword;
  public String ycqlAdminPassword;

  public String ysqlAdminUsername;
  public String ysqlCurrAdminPassword;
  public String ysqlAdminPassword;

  public String dbName;
}
