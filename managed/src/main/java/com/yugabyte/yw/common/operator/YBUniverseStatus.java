// // Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.operator;

@com.fasterxml.jackson.annotation.JsonInclude(
    com.fasterxml.jackson.annotation.JsonInclude.Include.NON_NULL)
@com.fasterxml.jackson.annotation.JsonPropertyOrder({"universeStatus"})
@com.fasterxml.jackson.databind.annotation.JsonDeserialize(
    using = com.fasterxml.jackson.databind.JsonDeserializer.None.class)
@javax.annotation.Generated("io.fabric8.java.generator.CRGeneratorRunner")
public class YBUniverseStatus implements io.fabric8.kubernetes.api.model.KubernetesResource {

  @com.fasterxml.jackson.annotation.JsonProperty("universeStatus")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String universeStatus;

  public String getUniverseStatus() {
    return universeStatus;
  }

  public void setUniverseStatus(String universeStatus) {
    this.universeStatus = universeStatus;
  }
}
