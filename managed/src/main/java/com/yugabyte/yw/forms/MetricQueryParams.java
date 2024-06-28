// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.metrics.MetricSettings;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

@ApiModel(description = "Metrics request data")
@Data
public class MetricQueryParams {
  @ApiModelProperty(value = "Metrics")
  private List<String> metrics;

  @Constraints.Required()
  @ApiModelProperty(value = "Start time", required = true)
  private Long start;

  @ApiModelProperty(value = "End time")
  private Long end;

  @ApiModelProperty(value = "Node prefix")
  private String nodePrefix;

  @ApiModelProperty(value = "Cluster UUIDs")
  private List<UUID> clusterUuids;

  @ApiModelProperty(value = "Region code")
  private List<String> regionCodes;

  @ApiModelProperty(value = "Availability zone code")
  private List<String> availabilityZones;

  @ApiModelProperty(value = "Node names")
  private List<String> nodeNames;

  @ApiModelProperty(value = "XCluster config UUID for replication lag queries")
  private UUID xClusterConfigUuid;

  @ApiModelProperty(value = "Table name")
  private String tableName;

  @ApiModelProperty(value = "Table id")
  private String tableId;

  @ApiModelProperty(value = "Stream id")
  private String streamId;

  @ApiModelProperty(value = "Is Recharts")
  @JsonProperty("isRecharts")
  private boolean isRecharts;

  @ApiModelProperty(value = "List of metrics with custom settings")
  private List<MetricSettings> metricsWithSettings;

  @ApiModelProperty(value = "Server type")
  private UniverseTaskBase.ServerType serverType;
}
