package com.yugabyte.yw.common.operator;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.fabric8.kubernetes.api.model.HasMetadata;
import java.util.Objects;
import lombok.Getter;
import lombok.NoArgsConstructor;

@NoArgsConstructor
@Getter
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

  public KubernetesResourceDetails(String name, String namespace) {
    this.name = name;
    this.namespace = namespace;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    KubernetesResourceDetails that = (KubernetesResourceDetails) o;
    return Objects.equals(resourceType, that.resourceType)
        && Objects.equals(namespace, that.namespace)
        && Objects.equals(name, that.name);
  }

  @Override
  public int hashCode() {
    if (resourceType == null) {
      return Objects.hash(namespace, name);
    }
    return Objects.hash(resourceType, namespace, name);
  }

  /**
   * Returns the resource name in "type:namespace:name" format, suitable for use as a unique
   * identifier in the operator_resource table.
   */
  public String toResourceName() {
    return String.format("%s:%s:%s", resourceType, namespace, name);
  }

  /**
   * Creates a KubernetesResourceDetails from a resource name in "type:namespace:name" format.
   *
   * @param resourceName the resource name in "type:namespace:name" format
   * @return the parsed KubernetesResourceDetails
   */
  public static KubernetesResourceDetails fromResourceName(String resourceName) {
    String[] parts = resourceName.split(":", 3);
    if (parts.length != 3) {
      throw new IllegalArgumentException(
          "Invalid resource name format, expected 'type:namespace:name': " + resourceName);
    }
    KubernetesResourceDetails details = new KubernetesResourceDetails();
    details.resourceType = parts[0];
    details.namespace = parts[1];
    details.name = parts[2];
    return details;
  }

  @Override
  public String toString() {
    return String.format(
        "KubernetesResourceDetails{type=%s, namespace=%s, name=%s}", resourceType, namespace, name);
  }
}
