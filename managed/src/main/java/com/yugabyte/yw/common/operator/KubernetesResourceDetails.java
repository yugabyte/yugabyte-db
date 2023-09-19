package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.fabric8.kubernetes.api.model.HasMetadata;

public class KubernetesResourceDetails {
  @JsonProperty("resourceType")
  public String resourceType;

  @JsonProperty("namespace")
  public String namespace;

  @JsonProperty("name")
  public String name;

  public static KubernetesResourceDetails fromResource(HasMetadata resource) {
    KubernetesResourceDetails krn = new KubernetesResourceDetails();
    krn.resourceType = resource.getSingular();
    krn.namespace = resource.getMetadata().getNamespace();
    krn.name = resource.getMetadata().getName();
    return krn;
  }
}
