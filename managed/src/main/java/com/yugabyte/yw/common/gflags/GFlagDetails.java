// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.common.gflags;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

/*
 * This class will be used to capture gflag details from metadata
 */
@JsonIgnoreProperties(ignoreUnknown = true)
public class GFlagDetails {
  @JsonProperty(value = "file")
  public String file;

  @JsonProperty(value = "name")
  public String name;

  @JsonProperty(value = "meaning")
  public String meaning;

  @JsonProperty(value = "default")
  public String defaultValue;

  @JsonProperty(value = "current")
  public String currentValue;

  @JsonProperty(value = "type")
  public String type;

  @JsonProperty(value = "tags")
  public String tags;

  @JsonProperty(value = "initial")
  public String initial;

  @JsonProperty(value = "target")
  public String target;
}
