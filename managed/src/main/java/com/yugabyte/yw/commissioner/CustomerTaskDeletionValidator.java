// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Decides whether a stale customer task can be garbage collected.
 *
 * <p>Create one instance per customer per GC run. The referenced task UUIDs are loaded once, on
 * first use.
 */
@Slf4j
public class CustomerTaskDeletionValidator {
  private final Customer customer;
  private Set<UUID> referencedTaskUuids;

  public CustomerTaskDeletionValidator(Customer customer) {
    this.customer = customer;
  }

  public boolean isDeletable(CustomerTask customerTask) {
    if (getReferencedTaskUuids().contains(customerTask.getTaskUUID())) {
      log.debug(
          "Customer task {} ({} {}) is not deletable as a universe still references it",
          customerTask.getTaskUUID(),
          customerTask.getTargetType(),
          customerTask.getTargetName());
      return false;
    }
    return customerTask.isDeletable();
  }

  private Set<UUID> getReferencedTaskUuids() {
    if (referencedTaskUuids == null) {
      referencedTaskUuids = fetchReferencedTaskUuids();
    }
    return referencedTaskUuids;
  }

  // Fetch all the references first. This handles cases like a task on source locking target in
  // XCluster.
  // If source is deleted, the task is deleted, But the target still has reference to the task.
  // This can break places where the task must exist if the task UUID is set.
  private Set<UUID> fetchReferencedTaskUuids() {
    Set<UUID> referenced = new HashSet<>();
    for (Universe universe : Universe.getAllWithoutResources(customer)) {
      UniverseDefinitionTaskParams details = universe.getUniverseDetails();
      if (details.updatingTaskUUID != null) {
        referenced.add(details.updatingTaskUUID);
      }
      if (details.placementModificationTaskUuid != null) {
        referenced.add(details.placementModificationTaskUuid);
      }
    }
    if (referenced.isEmpty()) {
      return referenced;
    }
    TaskInfo.find(new HashSet<>(referenced)).stream()
        .map(t -> t.getTaskParams().path("originalTaskUUID").asText(""))
        .filter(s -> !s.isEmpty())
        .map(UUID::fromString)
        .forEach(referenced::add);
    return referenced;
  }
}
