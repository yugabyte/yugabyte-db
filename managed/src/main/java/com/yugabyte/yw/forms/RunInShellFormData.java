// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class RunInShellFormData {
  public enum ShellType {
    YSQLSH,
    YCQLSH
  }

  public String command;

  public String command_file;

  @Constraints.Required() public String db_name;

  public ShellType shell_type = ShellType.YSQLSH;

  public String shell_location = null;
}
