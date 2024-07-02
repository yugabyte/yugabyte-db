// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.metrics.MetricSettings;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

@ApiModel(description = "Metrics request data")
@Data
public class MetricQueryParams {
  @ApiModelProperty(value = "YbaApi Internal. Metrics")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  private List<String> metrics;

  @Constraints.Required()
  @ApiModelProperty(value = "YbaApi Internal. Start time", required = true)
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  private Long start;

  @ApiModelProperty(value = "YbaApi Internal. End time")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  private Long end;

  @ApiModelProperty(value = "YbaApi Internal. Node prefix")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  private String nodePrefix;

  @ApiModelProperty(value = "YbaApi Internal. Cluster UUIDs")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private List<UUID> clusterUuids;

  @ApiModelProperty(value = "YbaApi Internal. Region code")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private List<String> regionCodes;

  @ApiModelProperty(value = "YbaApi Internal. Availability zone code")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private List<String> availabilityZones;

  @ApiModelProperty(value = "YbaApi Internal. Node names")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.8.0.0")
  private List<String> nodeNames;

  @ApiModelProperty(value = "YbaApi Internal. XCluster config UUID for replication lag queries")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private UUID xClusterConfigUuid;

  @ApiModelProperty(value = "YbaApi Internal. Table name")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private String tableName;

  @ApiModelProperty(value = "YbaApi Internal. Table id")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.4.0")
  private String tableId;

  @ApiModelProperty(value = "YbaApi Internal. Stream id")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.19.0.0")
  private String streamId;

  @ApiModelProperty(value = "YbaApi Internal. Is Recharts")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.0.0")
  @JsonProperty("isRecharts")
  private boolean isRecharts;

  @ApiModelProperty(value = "YbaApi Internal. List of metrics with custom settings")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.14.0.0")
  private List<MetricSettings> metricsWithSettings;

  @ApiModelProperty(value = "YbaApi Internal. Server type")
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.18.0.0")
  private UniverseTaskBase.ServerType serverType;
}
