// Copyright (c) YugaByte, Inc

package com.yugabyte.yw.common.gflags;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;

@ApiModel(value = "GFlagGroup", description = "GFlag Groups")
public class GFlagGroup {
  public enum GroupName {
    @EnumValue("PG_PARITY")
    PG_PARITY;
  }

  private static class ServerTypeFlags {
    @ApiModelProperty(value = "YbaApi Internal. Master GFlags")
    @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
    @JsonProperty(value = "MASTER")
    Map<String, String> masterGFlags;

    @ApiModelProperty(value = "YbaApi Internal. TServer GFlags")
    @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
    @JsonProperty(value = "TSERVER")
    Map<String, String> tserverGFlags;
  }

  @ApiModelProperty(value = "YbaApi Internal. GFlag Group Name", allowableValues = "PG_PARITY")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
  @JsonProperty(value = "group_name")
  public GroupName groupName;

  @ApiModelProperty(value = "YbaApi Internal. Flags belonging to the group")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
  @JsonProperty(value = "flags")
  public ServerTypeFlags flags;
}
