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
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
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

  @Column(nullable = false)
  private boolean deleted = false;

  public boolean isDeleted() {
    return deleted;
  }

  @Column(columnDefinition = "TEXT")
  private String platformInstances; // Comma-separated PlatformInstance UUIDs

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

  /** Get a live (non-deleted) OperatorResource by its name (type:namespace:name). */
  public static OperatorResource getByName(String name) {
    return find.query().where().eq("name", name).eq("deleted", false).findOne();
  }

  /** Get all live (non-deleted) tracked OperatorResources. */
  public static List<OperatorResource> getAll() {
    return find.query().where().eq("deleted", false).findList();
  }

  /** Get all soft-deleted OperatorResources (tombstones awaiting K8s cleanup on failover). */
  public static List<OperatorResource> getDeleted() {
    return find.query().where().eq("deleted", true).findList();
  }

  /**
   * Create or update an OperatorResource. If it already exists (including soft-deleted rows),
   * updates the data (YAML) and ensures {@code deleted} is {@code false}. Returns the persisted
   * resource.
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
      if (resource.isDeleted()) {
        log.info("Reviving previously soft-deleted OperatorResource: {}", name);
        resource.setDeleted(false);
      }
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
   * Soft-delete an OperatorResource and return the set of KubernetesResourceDetails for
   * dependencies that are no longer depended on by any other live resource (orphaned dependencies).
   * Orphaned dependencies are also soft-deleted. The local platform instance UUID is removed from
   * the resource (and its orphaned deps) since this K8s cluster no longer has the resource. The
   * rows remain in the database as tombstones so that the HA restorer can clean up corresponding
   * K8s resources on other clusters during failover.
   *
   * @param name the resource name
   * @param localInstanceUuid the local PlatformInstance UUID (may be null if HA is not configured)
   */
  @Transactional
  public static Set<KubernetesResourceDetails> deleteAndGetOrphaned(
      String name, UUID localInstanceUuid) {
    OperatorResource resource = find.byId(name);
    if (resource == null || resource.isDeleted()) {
      log.debug("Resource {} not found (or already deleted) for deletion", name);
      return Collections.emptySet();
    }

    Set<OperatorResource> removedDeps = new HashSet<>(resource.getDependencies());

    // Clear the dependency relationships, soft-delete, and remove local instance
    resource.getDependencies().clear();
    resource.setDeleted(true);
    if (localInstanceUuid != null) {
      Set<UUID> uuids = resource.getPlatformInstanceUuids();
      uuids.remove(localInstanceUuid);
      resource.setPlatformInstanceUuids(uuids);
    }
    resource.save();
    log.debug("Soft-deleted OperatorResource: {}", name);

    if (removedDeps.isEmpty()) {
      return Collections.emptySet();
    }

    // Find and soft-delete orphaned dependencies (no remaining live dependents)
    Set<KubernetesResourceDetails> orphaned = new HashSet<>();
    for (OperatorResource dep : removedDeps) {
      OperatorResource refreshed = find.byId(dep.getName());
      if (refreshed != null) {
        refreshed.refresh();
        boolean hasLiveDependents =
            refreshed.getDependents().stream().anyMatch(d -> !d.isDeleted());
        if (!hasLiveDependents) {
          orphaned.add(KubernetesResourceDetails.fromResourceName(dep.getName()));
          refreshed.getDependencies().clear();
          refreshed.setDeleted(true);
          if (localInstanceUuid != null) {
            Set<UUID> depUuids = refreshed.getPlatformInstanceUuids();
            depUuids.remove(localInstanceUuid);
            refreshed.setPlatformInstanceUuids(depUuids);
          }
          refreshed.save();
          log.debug("Soft-deleted orphaned dependency: {}", dep.getName());
        }
      }
    }
    return orphaned;
  }

  /** Returns the set of PlatformInstance UUIDs that have this resource applied to their K8s. */
  public Set<UUID> getPlatformInstanceUuids() {
    if (platformInstances == null || platformInstances.isBlank()) {
      return new HashSet<>();
    }
    return Arrays.stream(platformInstances.split(","))
        .map(String::trim)
        .filter(s -> !s.isEmpty())
        .map(UUID::fromString)
        .collect(Collectors.toCollection(HashSet::new));
  }

  /** Overwrites the set of PlatformInstance UUIDs. Sets to null when the set is empty. */
  public void setPlatformInstanceUuids(Set<UUID> uuids) {
    if (uuids == null || uuids.isEmpty()) {
      this.platformInstances = null;
    } else {
      this.platformInstances = uuids.stream().map(UUID::toString).collect(Collectors.joining(","));
    }
  }

  /**
   * Record that the given PlatformInstance has this resource applied in its K8s cluster. No-op if
   * {@code instanceUuid} is null.
   */
  @Transactional
  public static void addPlatformInstance(String name, UUID instanceUuid) {
    if (instanceUuid == null) {
      return;
    }
    OperatorResource resource = find.byId(name);
    if (resource != null) {
      Set<UUID> uuids = resource.getPlatformInstanceUuids();
      if (uuids.add(instanceUuid)) {
        resource.setPlatformInstanceUuids(uuids);
        resource.save();
        log.debug("Added platform instance {} to resource {}", instanceUuid, name);
      }
    }
  }

  /**
   * Remove the given PlatformInstance from this resource's instance set. If the set becomes empty,
   * hard-delete the row (no instance references it anymore). No-op if {@code instanceUuid} is null.
   *
   * @return true if the row was hard-deleted because no instances remain
   */
  @Transactional
  public static boolean removePlatformInstance(String name, UUID instanceUuid) {
    if (instanceUuid == null) {
      return false;
    }
    OperatorResource resource = find.byId(name);
    if (resource == null) {
      return false;
    }
    Set<UUID> uuids = resource.getPlatformInstanceUuids();
    if (uuids.remove(instanceUuid)) {
      resource.setPlatformInstanceUuids(uuids);
      resource.save();
      log.debug("Removed platform instance {} from resource {}", instanceUuid, name);
    }
    if (uuids.isEmpty()) {
      hardDelete(name);
      return true;
    }
    return false;
  }

  /** Permanently remove a resource row from the database (used after K8s cleanup on failover). */
  @Transactional
  public static void hardDelete(String name) {
    OperatorResource resource = find.byId(name);
    if (resource != null) {
      resource.delete();
      log.debug("Hard-deleted OperatorResource: {}", name);
    }
  }

  /** Check if a live (non-deleted) resource with the given name exists. */
  public static boolean exists(String name) {
    return find.query().where().eq("name", name).eq("deleted", false).exists();
  }
}
