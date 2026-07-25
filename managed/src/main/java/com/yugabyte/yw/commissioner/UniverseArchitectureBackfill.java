// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.AppInit;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.UniverseArchitectureResolver;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import io.ebean.DB;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

/**
 * Backfills {@code universeDetails.arch} for universes where it is unset. This covers on-prem and
 * other universes skipped by the V289 migration. Runs once on YBA startup.
 */
@Slf4j
@Singleton
public class UniverseArchitectureBackfill {

  private static final String LIVE_NODE_BACKFILL_SCHEDULER_NAME =
      UniverseArchitectureBackfill.class.getSimpleName() + ".liveNode";

  // Matches universes where arch is absent from JSON, explicitly null, or empty.
  // Use jsonb_exists() instead of the ? operator; JDBC treats ? as a bind placeholder.
  private static final String UNIVERSES_WITH_MISSING_ARCH_QUERY =
      "SELECT universe_uuid FROM universe"
          + " WHERE NOT jsonb_exists(universe_details_json::jsonb, 'arch')"
          + " OR universe_details_json::jsonb->'arch' IS NULL"
          + " OR TRIM(universe_details_json::jsonb->>'arch') = ''";

  private final PlatformScheduler platformScheduler;
  private final UniverseArchitectureResolver architectureResolver;

  @Inject
  public UniverseArchitectureBackfill(
      PlatformScheduler platformScheduler, UniverseArchitectureResolver architectureResolver) {
    this.platformScheduler = platformScheduler;
    this.architectureResolver = architectureResolver;
  }

  public void start() {
    List<UUID> universeUuids = fetchUniverseUuidsWithMissingArch();
    if (universeUuids == null || universeUuids.isEmpty()) {
      return;
    }
    List<UUID> unresolvedUuids = architectureResolver.resolveFromMetadata(universeUuids);
    if (unresolvedUuids == null || unresolvedUuids.isEmpty()) {
      return;
    }
    platformScheduler.scheduleOnce(
        LIVE_NODE_BACKFILL_SCHEDULER_NAME,
        Duration.ZERO,
        () -> backfillFromLiveNodes(unresolvedUuids));
  }

  private List<UUID> fetchUniverseUuidsWithMissingArch() {
    if (AppInit.isH2Db()) {
      // H2 does not support PostgreSQL jsonb operators used in unit tests.
      return Universe.getAllUUIDs().stream()
          .map(Universe::maybeGet)
          .flatMap(Optional::stream)
          .filter(u -> isArchUnpopulated(u.getUniverseDetails().arch))
          .map(Universe::getUniverseUUID)
          .collect(Collectors.toList());
    }
    return DB.sqlQuery(UNIVERSES_WITH_MISSING_ARCH_QUERY).findList().stream()
        .map(row -> (UUID) row.get("universe_uuid"))
        .collect(Collectors.toList());
  }

  private static boolean isArchUnpopulated(Architecture arch) {
    return arch == null;
  }

  @VisibleForTesting
  void backfillFromLiveNodes(List<UUID> universeUuids) {
    for (UUID universeUuid : universeUuids) {
      try {
        backfillFromLiveNodeIfNeeded(universeUuid);
      } catch (Exception e) {
        log.warn(
            "Failed to backfill architecture from live node for universe {}: {}",
            universeUuid,
            e.getMessage());
      }
    }
  }

  private void backfillFromLiveNodeIfNeeded(UUID universeUuid) {
    Universe universe = Universe.maybeGet(universeUuid).orElse(null);
    if (universe == null || !isArchUnpopulated(universe.getUniverseDetails().arch)) {
      return;
    }

    Optional<Architecture> resolved = architectureResolver.resolveFromLiveNode(universe);
    if (resolved.isEmpty()) {
      log.debug(
          "Could not resolve architecture from live node for universe {} ({})",
          universeUuid,
          universe.getName());
      return;
    }

    saveArchIfNull(universeUuid, resolved.get());
  }

  private void saveArchIfNull(UUID universeUuid, Architecture arch) {
    Universe.saveDetails(
        universeUuid,
        u -> {
          UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
          if (isArchUnpopulated(universeDetails.arch)) {
            universeDetails.arch = arch;
            u.setUniverseDetails(universeDetails);
            log.info("Set architecture {} on universe {} ({})", arch, universeUuid, u.getName());
          }
        });
  }
}
