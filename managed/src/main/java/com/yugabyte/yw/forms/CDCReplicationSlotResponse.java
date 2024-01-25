// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import lombok.Data;

@Data
@ApiModel(description = "CDC Replication slot details")
public class CDCReplicationSlotResponse {

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. CDC replication Slots list",
      accessMode = READ_ONLY)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  public List<CDCReplicationSlotDetails> replicationSlots = new ArrayList<>();

  public static class CDCReplicationSlotDetails {

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. CDC replication Slot stream id",
        accessMode = READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
    public String streamID;

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. CDC replication Slot name",
        accessMode = READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
    public String slotName;

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. CDC replication database name",
        accessMode = READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
    public String databaseName;

    @ApiModelProperty(
        value = "WARNING: This is a preview API that could change. CDC replication Slot state",
        accessMode = READ_ONLY)
    @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
    public String state;
  }
}
