// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import db.migration.default_.common.R__Redact_Secrets_From_Audit;
import io.ebean.DB;
import io.ebean.SqlRow;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/**
 * Scheduled task that redacts secrets from audit payloads and from the gflag changes in their
 * additional details. Each run processes all pending audit entries batch-wise until no more
 * batches. Uses an in-memory cursor (resets on restart). Already redacted rows are skipped (no
 * update). Migration checksum is used to determine if redaction is needed and to avoid re-redacting
 * already redacted.
 */
@Singleton
@Slf4j
public class RedactSecretsFromAudit {
  private static final AtomicInteger LAST_REDACTED_VERSION = new AtomicInteger(0);

  private volatile long lastProcessedId = 0L;

  private final PlatformScheduler platformScheduler;
  private final RedactingService redactingService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public RedactSecretsFromAudit(
      PlatformScheduler platformScheduler,
      RedactingService redactingService,
      RuntimeConfGetter confGetter) {
    this.platformScheduler = platformScheduler;
    this.redactingService = redactingService;
    this.confGetter = confGetter;
  }

  private void initializeLastRedactedVersion() {
    String migrationName =
        "db.migration.default_.common." + R__Redact_Secrets_From_Audit.class.getSimpleName();
    // Get the checksum of the last migration that redacts secrets from audit.
    String query =
        "SELECT checksum FROM schema_version WHERE script = '"
            + migrationName
            + "' ORDER BY installed_rank DESC LIMIT 1";
    Optional<SqlRow> optional = DB.sqlQuery(query).findOneOrEmpty();
    if (optional.isEmpty()) {
      log.error("Migration '{}' must exist in schema_version table", migrationName);
      throw new RuntimeException("Required migration not found: " + migrationName);
    }
    LAST_REDACTED_VERSION.set(optional.get().getInteger("checksum"));
    log.info("Last redacted version is set to '{}'", LAST_REDACTED_VERSION.get());
  }

  public void start() {
    initializeLastRedactedVersion();
    Duration interval = confGetter.getGlobalConf(GlobalConfKeys.bgRedactAuditInterval);
    platformScheduler.schedule(
        getClass().getSimpleName(), Duration.ZERO, interval, this::scheduleRunner);
  }

  public static int getLastRedactedVersion() {
    return LAST_REDACTED_VERSION.get();
  }

  private void scheduleRunner() {
    try {
      if (HighAvailabilityConfig.isFollower()) {
        log.debug("Skipping audit redaction for follower platform");
        return;
      }
      log.debug("Starting audit secrets redaction from last processed id {}", lastProcessedId);
      redact();
      log.debug("Completed audit secrets redaction batch till {}", lastProcessedId);
    } catch (Exception e) {
      log.error("Error running audit secrets redaction", e);
    }
  }

  /**
   * Runs redaction on all audit entries batch-wise until no more batches. Fetches pages with id >
   * lastProcessedId, redacts each, advances the cursor; repeats until a page is empty. Only selects
   * id and payload to avoid loading additionalDetails, which can be NULL in the DB and causes
   * Ebean's JsonNode scalar to throw when parsing.
   */
  private void redact() {
    int currentChecksum = LAST_REDACTED_VERSION.get();
    int batchSize = confGetter.getGlobalConf(GlobalConfKeys.bgRedactAuditBatchSize);
    while (true) {
      // Fetch a batch of audits that have not been redacted with the current checksum
      // and ids greater than the last processed id.
      List<Audit> batch =
          Audit.find
              .query()
              .select("id, payload")
              .where()
              .ne("last_redacted_version", currentChecksum)
              .gt("id", lastProcessedId)
              .orderBy("id asc")
              .setMaxRows(batchSize)
              .findList();
      log.debug(
          "Found '{}' audit entries to redact in batch with last processed id: '{}'",
          batch.size(),
          lastProcessedId);

      if (batch.isEmpty()) {
        return;
      }
      Set<Long> unredactedIds = redactGFlagChangesInAdditionalDetails(batch);
      long currentMaxId = lastProcessedId;
      List<Audit> toUpdate = new ArrayList<>();
      for (Audit audit : batch) {
        currentMaxId = Math.max(currentMaxId, audit.getId());
        if (unredactedIds.contains(audit.getId())) {
          continue;
        }
        JsonNode payload = audit.getPayload();
        if (payload != null && !payload.isNull() && !payload.isMissingNode()) {
          try {
            JsonNode newPayload =
                redactingService.filterSecretFields(payload, RedactionTarget.LOGS);
            if (audit.getLastRedactedVersion() != currentChecksum || !payload.equals(newPayload)) {
              audit.setPayload(newPayload);
              audit.setLastRedactedVersion(currentChecksum);
              toUpdate.add(audit);
            }
          } catch (Exception e) {
            log.warn("Failed to redact audit id {}", audit.getId(), e);
          }
        } else if (audit.getLastRedactedVersion() != currentChecksum) {
          audit.setLastRedactedVersion(currentChecksum);
          toUpdate.add(audit);
        }
      }
      if (!toUpdate.isEmpty()) {
        DB.updateAll(toUpdate);
      }
      lastProcessedId = currentMaxId;
    }
  }

  /**
   * Redacts the LDAP passwords (ycql_ldap_bind_passwd, and ldapbindpasswd in ysql_hba_conf_csv)
   * that audit entries written by older versions hold in plain text in the gflag changes in
   * additionalDetails. Reads the column with plain SQL for the reason given on {@link #redact()}.
   * Returns the ids of the entries it failed to redact; they must stay unmarked so that the job
   * retries them once the cursor resets.
   */
  private Set<Long> redactGFlagChangesInAdditionalDetails(List<Audit> batch) {
    Set<Long> unredactedIds = new HashSet<>();
    List<Long> ids = batch.stream().map(Audit::getId).toList();
    List<SqlRow> rows =
        DB.sqlQuery(
                "SELECT id, additional_details FROM audit"
                    + " WHERE id IN (:ids) AND additional_details IS NOT NULL")
            .setParameter("ids", ids)
            .findList();
    for (SqlRow row : rows) {
      Long id = row.getLong("id");
      try {
        JsonNode details = Json.parse(row.getString("additional_details"));
        JsonNode redacted = RedactingService.redactAuditGFlagChanges(details);
        if (!details.equals(redacted)) {
          DB.sqlUpdate("UPDATE audit SET additional_details = :details WHERE id = :id")
              .setParameter("details", Json.stringify(redacted))
              .setParameter("id", id)
              .execute();
        }
      } catch (Exception e) {
        log.warn("Failed to redact additional details of audit id {}", id, e);
        unredactedIds.add(id);
      }
    }
    return unredactedIds;
  }
}
