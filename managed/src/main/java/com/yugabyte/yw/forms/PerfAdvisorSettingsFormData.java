// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.PropertyNamingStrategy;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.Min;
import lombok.Data;
import lombok.experimental.Accessors;

/** Universe perf advisor settings. Fields are nullable - which means use default global value */
@ApiModel
@Data
@Accessors(chain = true)
@JsonNaming(PropertyNamingStrategy.SnakeCaseStrategy.class)
public class PerfAdvisorSettingsFormData {

  @ApiModelProperty(value = "Enable/disable perf advisor runs for the universe")
  private Boolean enabled;

  @Min(5)
  @ApiModelProperty(value = "Perf advisor runs frequency, in minutes")
  private Integer universeFrequencyMins;

  @ApiModelProperty(value = "Perf advisor connection skew threshold")
  @Min(1)
  private Double connectionSkewThresholdPct;

  @ApiModelProperty(value = "Perf advisor connection skew min connections")
  @Min(1)
  private Integer connectionSkewMinConnections;

  @ApiModelProperty(value = "Perf advisor connection skew check interval")
  @Min(1)
  private Integer connectionSkewIntervalMins;

  @ApiModelProperty(value = "Perf advisor cpu skew threshold")
  @Min(1)
  private Double cpuSkewThresholdPct;

  @ApiModelProperty(value = "Perf advisor cpu skew min cpu usage")
  @Min(1)
  private Double cpuSkewMinUsagePct;

  @ApiModelProperty(value = "Perf advisor cpu skew check interval")
  @Min(1)
  private Integer cpuSkewIntervalMins;

  @ApiModelProperty(value = "Perf advisor CPU usage threshold")
  @Min(1)
  private Double cpuUsageThreshold;

  @ApiModelProperty(value = "Perf advisor CPU usage check interval")
  @Min(1)
  private Integer cpuUsageIntervalMins;

  @ApiModelProperty(value = "Perf advisor query skew threshold")
  @Min(1)
  private Double querySkewThresholdPct;

  @ApiModelProperty(value = "Perf advisor query skew min queries")
  @Min(1)
  private Integer querySkewMinQueries;

  @ApiModelProperty(value = "Perf advisor query skew check interval")
  @Min(1)
  private Integer querySkewIntervalMins;

  @ApiModelProperty(value = "Perf advisor rejected connections threshold")
  @Min(1)
  private Integer rejectedConnThreshold;

  @ApiModelProperty(value = "Perf advisor rejected connections check interval")
  @Min(1)
  private Integer rejectedConnIntervalMins;

  @ApiModelProperty(value = "Perf Advisor hot shard write skew threshold")
  @Min(1)
  private Double hotShardWriteSkewThresholdPct;

  @ApiModelProperty(value = "Perf Advisor hot shard read skew threshold")
  @Min(1)
  private Double hotShardReadSkewThresholdPct;

  @ApiModelProperty(value = "Perf Advisor hot shard check interval")
  @Min(1)
  private Integer hotShardIntervalMins;

  @ApiModelProperty(value = "Perf advisor hot shard minimal node writes")
  @Min(1)
  private Integer hotShardMinNodeWrites;

  @ApiModelProperty(value = "Perf advisor hot shard minimal node reads")
  @Min(1)
  private Integer hotShardMinNodeReads;
}
