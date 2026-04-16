// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.JoinTable;
import jakarta.persistence.ManyToMany;
import jakarta.persistence.Table;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

/**
 * Represents a Kubernetes resource tracked by the YBA operator. The name is in
 * "type:namespace:name" format and serves as the primary key. The data column stores the raw
 * YAML/data of the resource for later application.
 *
 * <p>Dependencies between resources (e.g., a CR depending on Secrets) are modeled as a
 * self-referential many-to-many relationship via the operator_resource_dependency join table.
 */
@Entity
@Table(name = "operator_resource")
@Getter
@Setter
@Slf4j
@EqualsAndHashCode(callSuper = false, onlyExplicitlyIncluded = true)
public class OperatorResource extends Model {

  @Id
  @Column(nullable = false)
  @EqualsAndHashCode.Include
  private String name; // "type:namespace:name" format

  @Column(columnDefinition = "TEXT")
  private String data; // Raw YAML data

  @ToString.Exclude
  @ManyToMany(fetch = FetchType.LAZY)
  @JoinTable(
      name = "operator_resource_dependency",
      joinColumns =
          @JoinColumn(name = "resource_name", referencedColumnName = "name", nullable = false),
      inverseJoinColumns =
          @JoinColumn(name = "dependent_name", referencedColumnName = "name", nullable = false))
  private Set<OperatorResource> dependencies = new HashSet<>();

  @ToString.Exclude
  @ManyToMany(mappedBy = "dependencies", fetch = FetchType.LAZY)
  @JoinTable(
      name = "operator_resource_dependency",
      joinColumns = {
        @JoinColumn(name = "dependent_name", referencedColumnName = "name", nullable = false),
        @JoinColumn(name = "resource_name", referencedColumnName = "name", nullable = false)
      })
  private Set<OperatorResource> dependents = new HashSet<>();

  public static final Finder<String, OperatorResource> find = new Finder<>(OperatorResource.class);

  /** Get an OperatorResource by its name (type:namespace:name). */
  public static OperatorResource getByName(String name) {
    return find.byId(name);
  }

  /** Get all tracked OperatorResources. */
  public static List<OperatorResource> getAll() {
    return find.query().findList();
  }

  /**
   * Create or update an OperatorResource. If it already exists, updates the data (YAML). Returns
   * the persisted resource.
   */
  @Transactional
  public static OperatorResource createOrUpdate(String name, String yamlData) {
    OperatorResource resource = find.byId(name);
    if (resource == null) {
      resource = new OperatorResource();
      resource.setName(name);
      resource.setData(yamlData);
      resource.save();
      log.debug("Created OperatorResource: {}", name);
    } else {
      resource.setData(yamlData);
      resource.update();
      log.debug("Updated OperatorResource: {}", name);
    }
    return resource;
  }

  /**
   * Add a dependency relationship between two resources. Both resources must already exist in the
   * database.
   */
  @Transactional
  public static void addDependency(String ownerName, String dependentName) {
    OperatorResource owner = find.byId(ownerName);
    OperatorResource dependent = find.byId(dependentName);
    if (owner == null || dependent == null) {
      log.warn(
          "Cannot add dependency: owner={} (exists={}), dependent={} (exists={})",
          ownerName,
          owner != null,
          dependentName,
          dependent != null);
      return;
    }
    if (!owner.getDependencies().contains(dependent)) {
      owner.getDependencies().add(dependent);
      owner.save();
      log.debug("Added dependency {} -> {}", ownerName, dependentName);
    }
  }

  /**
   * Delete an OperatorResource and return the set of KubernetesResourceDetails for dependencies
   * that are no longer depended on by any other resource (orphaned dependencies). Orphaned
   * dependencies are also removed from the database.
   */
  @Transactional
  public static Set<KubernetesResourceDetails> deleteAndGetOrphaned(String name) {
    OperatorResource resource = find.byId(name);
    if (resource == null) {
      log.debug("Resource {} not found for deletion", name);
      return Collections.emptySet();
    }

    Set<OperatorResource> removedDeps = new HashSet<>(resource.getDependencies());

    // Clear the dependency relationships before deleting
    resource.getDependencies().clear();
    resource.save();
    resource.delete();
    log.debug("Deleted OperatorResource: {}", name);

    if (removedDeps.isEmpty()) {
      return Collections.emptySet();
    }

    // Find and delete orphaned dependencies (no longer depended on by any other resource)
    Set<KubernetesResourceDetails> orphaned = new HashSet<>();
    for (OperatorResource dep : removedDeps) {
      // Refresh from DB to get current dependents
      OperatorResource refreshed = find.byId(dep.getName());
      if (refreshed != null) {
        // Force refresh to pick up latest join table state
        refreshed.refresh();
        if (refreshed.getDependents().isEmpty()) {
          orphaned.add(KubernetesResourceDetails.fromResourceName(dep.getName()));
          // Clean up this orphan's own dependencies before deleting
          refreshed.getDependencies().clear();
          refreshed.save();
          refreshed.delete();
          log.debug("Deleted orphaned dependency: {}", dep.getName());
        }
      }
    }
    return orphaned;
  }

  /** Check if a resource with the given name exists. */
  public static boolean exists(String name) {
    return find.byId(name) != null;
  }
}
