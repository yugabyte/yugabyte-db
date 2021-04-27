// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.List;


public class MetricQueryParams {
  @Constraints.Required()
  private List<String> metrics;

  @Constraints.Required()
  private Long start;

  private Long end;

  private String nodePrefix;

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
