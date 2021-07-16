// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import play.data.validation.Constraints;

@ApiModel(value = "Metrics", description = "Metrics details")
public class MetricQueryParams {
  @Constraints.Required()
  @ApiModelProperty(value = "metrics", required = true)
  private List<String> metrics;

  @Constraints.Required()
  @ApiModelProperty(value = "Start time", required = true)
  private Long start;

  @ApiModelProperty(value = "End time")
  private Long end;

  @ApiModelProperty(value = "Node prefix")
  private String nodePrefix;

  @ApiModelProperty(value = "Node name")
  private String nodeName;

  public List<String> getMetrics() {
    return metrics;
  }

  public void setMetrics(List<String> metrics) {
    this.metrics = metrics;
  }

  public Long getStart() {
    return start;
  }

  public void setStart(Long start) {
    this.start = start;
  }

  public Long getEnd() {
    return end;
  }

  public void setEnd(Long end) {
    this.end = end;
  }

  public String getNodePrefix() {
    return nodePrefix;
  }

  public void setNodePrefix(String nodePrefix) {
    this.nodePrefix = nodePrefix;
  }

  public String getNodeName() {
    return nodeName;
  }

  public void setNodeName(String nodeName) {
    this.nodeName = nodeName;
  }
}
