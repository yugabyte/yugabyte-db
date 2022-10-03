// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import com.fasterxml.jackson.annotation.JsonProperty;

public class GFlagDiffEntry {
  @JsonProperty(value = "name")
  public String name;

  @JsonProperty(value = "old")
  public String oldValue;

  @JsonProperty(value = "new")
  public String newValue;

  @JsonProperty(value = "default")
  public String defaultValue;

  public GFlagDiffEntry(String name, String oldValue, String newValue, String defaultValue) {
    this.name = name;
    this.oldValue = oldValue;
    this.newValue = newValue;
    this.defaultValue = defaultValue;
  }
}
