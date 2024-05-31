// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import javax.validation.constraints.NotNull;

public class CustomCACertParams {

  @NotNull private String name;

  @NotNull private String contents;

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public String getContents() {
    return contents;
  }

  public void setContents(String contents) {
    this.contents = contents;
  }
}
