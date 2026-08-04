// Copyright (c) YugabyteDB, Inc

package com.yugabyte.yw.common.gflags;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModelProperty;

/*
 * This class will be used to capture gflag details from metadata
 */
@JsonIgnoreProperties(ignoreUnknown = true)
public class GFlagDetails {
  @JsonProperty(value = "file")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. File where the gflag is defined")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String file;

  @JsonProperty(value = "name")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change. Name of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String name;

  @JsonProperty(value = "meaning")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Meaning of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String meaning;

  @JsonProperty(value = "default")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Default value of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String defaultValue;

  @JsonProperty(value = "current")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Current value of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String currentValue;

  @JsonProperty(value = "type")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change. Type of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String type;

  @JsonProperty(value = "tags")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change. Tags of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String tags;

  @JsonProperty(value = "initial")
  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Initial value of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String initial;

  @JsonProperty(value = "target")
  @ApiModelProperty(value = "WARNING: This is a preview API that could change. Target of the gflag")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  public String target;
}
