package com.yugabyte.troubleshoot.ts.models;

import lombok.Getter;

@Getter
public enum GraphFilter {
  queryId(""),
  dbId(""),
  universeUuid("universe_uuid"),
  instanceName("exported_instance"),
  namespace("namespace"),
  podName("pod_name"),
  containerName("container_name"),
  pvc("persistentvolumeclaim"),
  clusterUuid(""),
  regionCode(""),
  azCode(""),
  instanceType(""),
  tableName("table_name"),
  tableId("table_id"),
  mountPoint("mountpoint");

  private final String metricLabel;

  GraphFilter(String metricLabel) {
    this.metricLabel = metricLabel;
  }
}
