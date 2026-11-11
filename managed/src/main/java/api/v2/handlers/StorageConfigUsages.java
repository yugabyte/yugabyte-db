// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.StorageConfigUsage;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.stream.Collectors;

/**
 * What uses each of a customer's storage configs: in-progress or completed backups stored with it,
 * active backup schedules that write to it, and DR configs that use it. Any of these makes the
 * config in use, as each blocks its deletion.
 *
 * <p>One query per kind for a whole set of configs: backups and DR configs by their indexed
 * storage_config_uuid, schedules among the customer's active backup schedules, whose storage config
 * is only in their JSON task params.
 */
final class StorageConfigUsages {

  private StorageConfigUsages() {}

  private static final class Usage {
    final Set<UUID> universes = new TreeSet<>();
    final Set<UUID> schedules = new TreeSet<>();
    final Set<UUID> drConfigs = new TreeSet<>();
    boolean inUse;
  }

  /**
   * The usage of each of the given storage configs of the customer. Universes that no longer exist
   * are left out of universe_uuids, but their backups still make the config in use.
   */
  static Map<UUID, StorageConfigUsage> of(UUID customerUUID, Collection<UUID> configUuids) {
    Map<UUID, Usage> usages = find(customerUUID, configUuids);
    Set<UUID> universeUuids =
        usages.values().stream()
            .flatMap(usage -> usage.universes.stream())
            .collect(Collectors.toSet());
    Set<UUID> existing =
        universeUuids.isEmpty()
            ? Set.of()
            : Universe.getAllWithoutResources(universeUuids).stream()
                .map(Universe::getUniverseUUID)
                .collect(Collectors.toSet());
    Map<UUID, StorageConfigUsage> result = new HashMap<>();
    for (UUID configUuid : configUuids) {
      Usage usage = usages.getOrDefault(configUuid, new Usage());
      result.put(
          configUuid,
          new StorageConfigUsage()
              .inUse(usage.inUse)
              .universeUuids(
                  usage.universes.stream().filter(existing::contains).collect(Collectors.toList()))
              .scheduleUuids(new ArrayList<>(usage.schedules))
              .drConfigUuids(new ArrayList<>(usage.drConfigs)));
    }
    return result;
  }

  /** Which of the given storage configs of the customer are in use. */
  static Set<UUID> inUse(UUID customerUUID, Collection<UUID> configUuids) {
    return find(customerUUID, configUuids).entrySet().stream()
        .filter(entry -> entry.getValue().inUse)
        .map(Map.Entry::getKey)
        .collect(Collectors.toSet());
  }

  private static Map<UUID, Usage> find(UUID customerUUID, Collection<UUID> configUuids) {
    Map<UUID, Usage> usages = new HashMap<>();
    if (configUuids.isEmpty()) {
      return usages;
    }
    List<Backup> backups =
        Backup.find
            .query()
            .setDistinct(true)
            .select("storageConfigUUID, universeUUID")
            .where()
            .in("storageConfigUUID", configUuids)
            .eq("customerUUID", customerUUID)
            .in("state", BackupState.InProgress, BackupState.Completed)
            .findList();
    for (Backup backup : backups) {
      Usage usage = usages.computeIfAbsent(backup.getStorageConfigUUID(), k -> new Usage());
      usage.inUse = true;
      usage.universes.add(backup.getUniverseUUID());
    }
    for (Schedule schedule : Schedule.getActiveBackupSchedules(customerUUID)) {
      UUID configUuid = uuidOrNull(schedule.getTaskParams().path("storageConfigUUID").asText());
      if (configUuid == null || !configUuids.contains(configUuid)) {
        continue;
      }
      Usage usage = usages.computeIfAbsent(configUuid, k -> new Usage());
      usage.inUse = true;
      usage.schedules.add(schedule.getScheduleUUID());
      UUID universeUuid = uuidOrNull(schedule.getTaskParams().path("universeUUID").asText());
      if (universeUuid != null) {
        usage.universes.add(universeUuid);
      }
    }
    for (DrConfig drConfig : DrConfig.getByStorageConfigUuids(configUuids)) {
      Usage usage = usages.computeIfAbsent(drConfig.getStorageConfigUuid(), k -> new Usage());
      usage.inUse = true;
      usage.drConfigs.add(drConfig.getUuid());
      XClusterConfig xClusterConfig =
          drConfig.hasActiveXClusterConfig() ? drConfig.getActiveXClusterConfig() : null;
      if (xClusterConfig != null) {
        if (xClusterConfig.getSourceUniverseUUID() != null) {
          usage.universes.add(xClusterConfig.getSourceUniverseUUID());
        }
        if (xClusterConfig.getTargetUniverseUUID() != null) {
          usage.universes.add(xClusterConfig.getTargetUniverseUUID());
        }
      }
    }
    return usages;
  }

  private static UUID uuidOrNull(String value) {
    try {
      return value.isEmpty() ? null : UUID.fromString(value);
    } catch (IllegalArgumentException e) {
      return null;
    }
  }
}
