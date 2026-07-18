// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.HasMetadata;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/**
 * Tracks YBA resources and their dependent resources (e.g., secrets) by persisting them to
 * PostgreSQL via the {@link OperatorResource} model.
 *
 * <p>Each YBA resource (a Kubernetes CR managed by a reconciler) can depend on zero or more other
 * resources (typically Kubernetes Secrets). This class maintains that mapping so that:
 *
 * <ul>
 *   <li>We know which secrets are used by which YBA resources.
 *   <li>When a YBA resource is deleted, we can safely remove it and any of its dependencies that
 *       are not used by other YBA resources.
 *   <li>All resource data (YAML) is persisted in the database for later application.
 * </ul>
 */
@Slf4j
public class ResourceTracker {

  /**
   * Track a YBA resource (the CR itself) with its YAML data. Creates or updates the resource in the
   * database.
   *
   * @param resource the Kubernetes resource to track
   */
  public void trackResource(HasMetadata resource) {
    KubernetesResourceDetails details = KubernetesResourceDetails.fromResource(resource);
    String resourceName = details.toResourceName();
    String yaml = Serialization.asYaml(resource);
    OperatorResource.createOrUpdate(resourceName, yaml);
    log.trace("Tracking resource: {}", resourceName);
  }

  /**
   * Track a YBA resource using pre-computed details and YAML data. Creates or updates the resource
   * in the database.
   *
   * @param details the resource details
   * @param yamlData the YAML representation of the resource
   */
  public void trackResource(KubernetesResourceDetails details, String yamlData) {
    String resourceName = details.toResourceName();
    OperatorResource.createOrUpdate(resourceName, yamlData);
    log.trace("Tracking resource: {}", resourceName);
  }

  /**
   * Track a dependent resource (e.g., a secret) associated with a YBA resource (the owner). Both
   * resources are created/updated in the database and the dependency relationship is recorded. The
   * dependency's YAML is automatically serialized from the HasMetadata object.
   *
   * @param owner the owner resource details
   * @param dependency the dependent Kubernetes resource (e.g., a Secret)
   */
  public void trackDependency(KubernetesResourceDetails owner, HasMetadata dependency) {
    KubernetesResourceDetails depDetails = KubernetesResourceDetails.fromResource(dependency);
    String depYaml = Serialization.asYaml(dependency);
    trackDependency(owner, depDetails, depYaml);
  }

  /**
   * Track a dependent resource using pre-computed details. Both resources are created/updated in
   * the database and the dependency relationship is recorded.
   *
   * @param owner the owner resource details
   * @param dependency the dependent resource details
   * @param dependencyYamlData the YAML representation of the dependency (may be null)
   */
  public void trackDependency(
      KubernetesResourceDetails owner,
      KubernetesResourceDetails dependency,
      String dependencyYamlData) {
    String ownerName = owner.toResourceName();
    String depName = dependency.toResourceName();

    // Ensure owner exists (it should already from trackResource)
    if (!OperatorResource.exists(ownerName)) {
      OperatorResource.createOrUpdate(ownerName, null);
    }
    OperatorResource.createOrUpdate(depName, dependencyYamlData);
    OperatorResource.addDependency(ownerName, depName);
    log.trace("Tracking dependency {} for owner {}", depName, ownerName);
  }

  /**
   * Track a dependent resource using pre-computed details, without YAML data. Convenience overload
   * for cases where the raw YAML is not available.
   *
   * @param owner the owner resource details
   * @param dependency the dependent resource details
   */
  public void trackDependency(
      KubernetesResourceDetails owner, KubernetesResourceDetails dependency) {
    trackDependency(owner, dependency, null);
  }

  /**
   * Untrack a YBA resource. Returns the set of dependent resources that are no longer used by any
   * remaining tracked resource and can safely be cleaned up.
   *
   * @param resource the YBA resource to untrack
   * @return the set of orphaned dependencies (no longer used by any other tracked resource)
   */
  public Set<KubernetesResourceDetails> untrackResource(KubernetesResourceDetails resource) {
    String resourceName = resource.toResourceName();
    log.trace("Untracking resource: {}", resourceName);
    Set<KubernetesResourceDetails> orphaned = OperatorResource.deleteAndGetOrphaned(resourceName);
    if (!orphaned.isEmpty()) {
      log.info("Orphaned dependencies after untracking {}: {}", resourceName, orphaned);
    }
    return orphaned;
  }

  /**
   * Get all tracked resources (both YBA resources and their dependencies) as a flat set. This is
   * useful for determining what resources this reconciler is currently managing or depending on.
   */
  public Set<KubernetesResourceDetails> getTrackedResources() {
    return Collections.unmodifiableSet(
        OperatorResource.getAll().stream()
            .map(r -> KubernetesResourceDetails.fromResourceName(r.getName()))
            .collect(Collectors.toSet()));
  }

  /** Get the full dependency map for inspection/debugging. */
  public Map<KubernetesResourceDetails, Set<KubernetesResourceDetails>> getResourceDependencies() {
    Map<KubernetesResourceDetails, Set<KubernetesResourceDetails>> result = new HashMap<>();
    for (OperatorResource r : OperatorResource.getAll()) {
      KubernetesResourceDetails key = KubernetesResourceDetails.fromResourceName(r.getName());
      Set<KubernetesResourceDetails> deps =
          Collections.unmodifiableSet(
              r.getDependencies().stream()
                  .map(d -> KubernetesResourceDetails.fromResourceName(d.getName()))
                  .collect(Collectors.toSet()));
      result.put(key, deps);
    }
    return Collections.unmodifiableMap(result);
  }

  /** Get the dependencies for a specific owner resource. */
  public Set<KubernetesResourceDetails> getDependencies(KubernetesResourceDetails owner) {
    OperatorResource resource = OperatorResource.getByName(owner.toResourceName());
    if (resource == null) {
      return Collections.emptySet();
    }
    return Collections.unmodifiableSet(
        resource.getDependencies().stream()
            .map(d -> KubernetesResourceDetails.fromResourceName(d.getName()))
            .collect(Collectors.toSet()));
  }

  /** Check if a specific resource is tracked (either as an owner or as a dependency). */
  public boolean isTracked(KubernetesResourceDetails resource) {
    return OperatorResource.exists(resource.toResourceName());
  }
}
