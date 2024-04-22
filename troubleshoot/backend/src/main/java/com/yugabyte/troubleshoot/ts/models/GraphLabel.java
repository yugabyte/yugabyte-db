package com.yugabyte.troubleshoot.ts.models;

import lombok.Getter;

@Getter
public enum GraphLabel {
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
  dbName("namespace_name"),
  tableName("table_name"),
  tableId("table_id"),
  mountPoint("mountpoint"),
  waitEventComponent(""),
  waitEventClass(""),
  waitEvent(""),
  clientNodeIp("");

  private final String metricLabel;

  GraphLabel(String metricLabel) {
    this.metricLabel = metricLabel;
  }
}
